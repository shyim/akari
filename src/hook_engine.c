#include "profiler_internal.h"
#include "hook_registry.h"

/*
 * Engine-level hooks — Zend Engine internal hooks beyond execute_ex/internal.
 *
 *   - zend_compile_file: records compilation time per file (useful with opcache off)
 *   - GC collect cycles: records time spent in garbage collection
 *   - Stack trace capture: attaches backtrace to spans on demand
 *
 * These are heavier-weight hooks, guarded by INI settings
 * (akari.trace_compile, akari.trace_gc).
 */

/* ── Original engine function pointers ── */

static zend_op_array *(*original_compile_file)(zend_file_handle *file_handle, int type) = NULL;
static int (*original_gc_collect_cycles)(void) = NULL;

/* ── Engine attr helpers ── */

static profiler_db_attr_t *engine_db_attr_alloc(profiler_state_t *state,
                                                 uint32_t span_index,
                                                 const char *system)
{
    if (state->db_attr_count >= state->db_attr_capacity) {
        size_t new_cap = state->db_attr_capacity
            ? state->db_attr_capacity * 2
            : PROFILER_INITIAL_DB_ATTRS;
        profiler_db_attr_t *new_attrs = realloc(state->db_attrs,
            new_cap * sizeof(profiler_db_attr_t));
        if (!new_attrs) return NULL;
        state->db_attrs = new_attrs;
        state->db_attr_capacity = new_cap;
    }

    profiler_db_attr_t *attr = &state->db_attrs[state->db_attr_count++];
    memset(attr, 0, sizeof(profiler_db_attr_t));
    attr->span_index = span_index;
    snprintf(attr->db_system, sizeof(attr->db_system), "%s", system);
    return attr;
}

static void engine_db_attr_set_statement(profiler_db_attr_t *attr,
                                          const char *statement,
                                          size_t statement_len)
{
    if (!attr || !statement || statement_len == 0) return;

    size_t copy_len = statement_len;
    if (copy_len >= sizeof(attr->db_statement)) {
        copy_len = sizeof(attr->db_statement) - 1;
    }

    memcpy(attr->db_statement, statement, copy_len);
    attr->db_statement[copy_len] = '\0';
    attr->db_statement_len = copy_len;
}

/* ── Compile file hook ──
 *
 * Wraps zend_compile_file to measure compilation time per file.
 * Creates spans inside the current trace state (if active).
 * Useful for detecting slow compilation (large files, many includes).
 */

static zend_op_array *profiler_compile_file(zend_file_handle *file_handle, int type)
{
    profiler_state_t *state = g_state;
    uint64_t t_start = realtime_ns();

    zend_op_array *result = original_compile_file(file_handle, type);

    if (state && state->active) {
        uint64_t t_end = realtime_ns();
        uint64_t elapsed = t_end - t_start;

        /* Only record if compilation took >1ms */
        if (elapsed >= 1000000) {
            if (!ensure_span_capacity(state)) {
                return result;
            }

            size_t span_idx = state->span_count++;
            profiler_span_t *span = &state->spans[span_idx];
            memset(span, 0, sizeof(profiler_span_t));

            memcpy(span->trace_id, state->trace_id, 32);
            profiler_generate_hex_id(state, span->span_id, 16);
            span->start_time_ns = t_start;
            span->end_time_ns = t_end;
            span->depth = state->stack_depth;
            span->kind = SPAN_KIND_INTERNAL;
            span->status_code = SPAN_STATUS_UNSET;

            /* Parent = current root if active */
            if (state->root.active) {
                memcpy(span->parent_span_id, state->root.span_id, 16);
                span->has_parent = 1;
            }

            /* Record filename as attribute. Twig compiles each template to a
             * PHP class under a ".../twig/" cache directory; tag those as
             * "twig" so template compilation is grouped apart from ordinary
             * file compilation. The template name itself is not available yet
             * (the class is not loaded until the file executes), so the cache
             * path is the best label at this point. */
            if (file_handle && file_handle->filename) {
                const char *fname = ZSTR_VAL(file_handle->filename);
                const char *system = strstr(fname, "/twig/") ? "twig" : "compile";
                profiler_db_attr_t *attr = engine_db_attr_alloc(state,
                    (uint32_t)span_idx, system);
                engine_db_attr_set_statement(attr, fname,
                    ZSTR_LEN(file_handle->filename));
            }

            maybe_flush(state);
        }
    }

    return result;
}

/* ── GC collect cycles hook ──
 *
 * Hooks zend_gc_collect_cycles to measure GC time.
 * PHP's GC can be a significant source of latency in apps with many
 * circular references.
 */

static int profiler_gc_collect_cycles(void)
{
    profiler_state_t *state = g_state;
    uint64_t t_start = realtime_ns();

    int collected = original_gc_collect_cycles();

    if (state && state->active) {
        uint64_t t_end = realtime_ns();
        uint64_t elapsed = t_end - t_start;

        /* Only record if GC took >5ms (GC is typically fast) */
        if (elapsed >= 5000000) {
            if (!ensure_span_capacity(state)) {
                return collected;
            }

            size_t span_idx = state->span_count++;
            profiler_span_t *span = &state->spans[span_idx];
            memset(span, 0, sizeof(profiler_span_t));

            memcpy(span->trace_id, state->trace_id, 32);
            profiler_generate_hex_id(state, span->span_id, 16);
            span->start_time_ns = t_start;
            span->end_time_ns = t_end;
            span->depth = state->stack_depth;
            span->kind = SPAN_KIND_INTERNAL;
            span->status_code = SPAN_STATUS_UNSET;

            if (state->root.active) {
                memcpy(span->parent_span_id, state->root.span_id, 16);
                span->has_parent = 1;
            }

            /* Record GC stats as attribute */
            profiler_db_attr_t *attr = engine_db_attr_alloc(state,
                (uint32_t)span_idx, "gc");
            if (attr) {
                int n = snprintf(attr->db_statement, sizeof(attr->db_statement),
                    "collected %d roots in %.2fms",
                    collected, elapsed / 1000000.0);
                attr->db_statement_len = profiler_clamp_snprintf_len(n,
                    sizeof(attr->db_statement));
            }

            maybe_flush(state);
        }
    }

    return collected;
}

/* ── Stack trace capture ──
 *
 * Captures a PHP backtrace and attaches it as a span event.
 * This is expensive — only called explicitly or for slow spans.
 */

#define STACKTRACE_MAX_DEPTH 32
#define STACKTRACE_MAX_FRAMESIZE 512

void profiler_capture_stacktrace(profiler_state_t *state, uint32_t span_index)
{
    if (!state) return;

    zval trace;
    array_init(&trace);

    /* Walk the current call stack */
    zend_execute_data *ex = EG(current_execute_data);
    int depth = 0;

    while (ex && depth < STACKTRACE_MAX_DEPTH) {
        zend_function *func = ex->func;
        if (!func) break;

        zval frame;
        array_init(&frame);

        if (func->common.function_name) {
            add_assoc_string(&frame, "function", ZSTR_VAL(func->common.function_name));
        }

        if (func->common.scope) {
            add_assoc_string(&frame, "class", ZSTR_VAL(func->common.scope->name));
        }

        if (func->type == ZEND_USER_FUNCTION) {
            zend_op_array *op_array = &func->op_array;
            if (op_array->filename) {
                add_assoc_string(&frame, "file", ZSTR_VAL(op_array->filename));
                uint32_t lineno = ex->opline ? ex->opline->lineno : 0;
                add_assoc_long(&frame, "line", lineno);
            }
        }

        add_next_index_zval(&trace, &frame);
        depth++;

        ex = ex->prev_execute_data;
        if (!ex || ex == EG(current_execute_data)) break;
    }

    /* Store in exception_events as a span event */
    if (state->exception_event_count < state->exception_event_capacity) {
        profiler_exception_event_t *ev = &state->exception_events[state->exception_event_count];
        ev->span_index = span_index;
        ev->timestamp_ns = realtime_ns();
        snprintf(ev->exception_type, EXCEPTION_TYPE_MAX, "stacktrace");
        snprintf(ev->exception_message, EXCEPTION_MESSAGE_MAX, "%d frames captured", depth);
        state->exception_event_count++;
    }

    zval_ptr_dtor(&trace);
}

/* ── Exception hook ──
 *
 * Hooks zend_throw_exception_hook to capture ALL exceptions at engine level.
 * This catches exceptions even before framework error handlers fire,
 * and catches exceptions that are caught and handled silently.
 */

static void (*original_throw_exception_hook)(zend_object *exception) = NULL;

static void profiler_throw_exception_hook(zend_object *exception)
{
    profiler_state_t *state = g_state;

    if (state && state->active && exception) {
        zend_class_entry *ce = exception->ce;

        /* Find the current span: last real entry on the stack. */
        uint32_t span_index;
        int has_current_span = profiler_current_span_index(state, &span_index);

        /* Record exception event */
        if (has_current_span && state->exception_event_count >= state->exception_event_capacity) {
            size_t new_cap = state->exception_event_capacity
                ? state->exception_event_capacity * 2
                : PROFILER_INITIAL_EXCEPTION_EVENTS;
            profiler_exception_event_t *new_events = realloc(state->exception_events,
                new_cap * sizeof(profiler_exception_event_t));
            if (new_events) {
                state->exception_events = new_events;
                state->exception_event_capacity = new_cap;
            }
        }

        if (has_current_span && state->exception_event_count < state->exception_event_capacity) {
            profiler_exception_event_t *ev = &state->exception_events[state->exception_event_count];
            memset(ev, 0, sizeof(profiler_exception_event_t));
            ev->span_index = span_index;
            ev->timestamp_ns = realtime_ns();
            /* Mark pending: at throw time we cannot know whether the
             * application will catch this. observer_fcall_end resolves it.
             * Keep the object pointer to match against the live EG(exception)
             * when deciding which pending event actually escaped. */
            ev->pending = 1;
            ev->exception_obj = exception;

            if (ce && ce->name) {
                snprintf(ev->exception_type, EXCEPTION_TYPE_MAX, "%s", ZSTR_VAL(ce->name));
            } else {
                snprintf(ev->exception_type, EXCEPTION_TYPE_MAX, "Exception");
            }

            /* Extract exception message */
            zval rv;
            zval *msg = zend_read_property_ex(ce, exception,
                            ZSTR_KNOWN(ZEND_STR_MESSAGE), 1, &rv);
            if (msg && Z_TYPE_P(msg) == IS_STRING) {
                size_t len = Z_STRLEN_P(msg);
                if (len >= EXCEPTION_MESSAGE_MAX) len = EXCEPTION_MESSAGE_MAX - 1;
                memcpy(ev->exception_message, Z_STRVAL_P(msg), len);
                ev->exception_message[len] = '\0';
            }

            state->exception_event_count++;
        }

        /* NOTE: span/root status is NOT flipped to ERROR here. The exception
         * may be caught and handled by application code, in which case it is
         * not an error. Status is set only if the exception actually escapes
         * (resolved in observer_fcall_end / request shutdown). */
    }

    /* Chain to original hook (Xdebug, other profilers) */
    if (original_throw_exception_hook) {
        original_throw_exception_hook(exception);
    }
}

/* ── Lifecycle ── */

void engine_hooks_init(int trace_compile, int trace_gc)
{
    /* Install compile_file hook if enabled */
    if (trace_compile) {
        if (!original_compile_file) {
            original_compile_file = zend_compile_file;
        }
        zend_compile_file = profiler_compile_file;
    }

    /* Install GC hook if enabled */
    if (trace_gc) {
        if (!original_gc_collect_cycles) {
            original_gc_collect_cycles = gc_collect_cycles;
        }
        gc_collect_cycles = profiler_gc_collect_cycles;
    }

    /* Install exception hook — always active, zero overhead when profiling off */
    if (!original_throw_exception_hook) {
        original_throw_exception_hook = zend_throw_exception_hook;
    }
    zend_throw_exception_hook = profiler_throw_exception_hook;
}

void engine_hooks_shutdown(void)
{
    if (original_compile_file) {
        zend_compile_file = original_compile_file;
        original_compile_file = NULL;
    }

    if (original_gc_collect_cycles) {
        gc_collect_cycles = original_gc_collect_cycles;
        original_gc_collect_cycles = NULL;
    }

    if (original_throw_exception_hook) {
        zend_throw_exception_hook = original_throw_exception_hook;
        original_throw_exception_hook = NULL;
    }
}
