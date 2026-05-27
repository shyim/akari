#include "profiler_internal.h"
#include "hook_registry.h"
#include "Zend/zend_observer.h"

/*
 * observer.c — zend_observer-based instrumentation (PHP 8.x native API).
 *
 * Replaces the classic zend_execute_ex / zend_execute_internal function pointer
 * hooks with the officially-supported zend_observer API. This means:
 *
 *   - No function pointer swapping (no conflicts with Xdebug, other profilers)
 *   - Built-in fiber support
 *   - Single callback pair (begin + end) for both userland and internal functions
 *   - Registered once at MINIT, active for the lifetime of the process
 *
 * The observer_init callback is called for every function invocation. It returns
 * the begin/end handlers only when profiling is active (g_state->active).
 * When profiling is off, it returns {NULL, NULL} — observer overhead is zero
 * because PHP skips functions with no handlers.
 *
 * Architecture:
 *   init    → check g_state->active → return handlers or {NULL, NULL}
 *   begin   → lookup hook / check trace flags → create span → pre_hook → push
 *   end     → pop → set end time → check thresholds → post_hook → maybe flush
 */

/* ── Forward declarations ── */

static void observer_fcall_begin(zend_execute_data *execute_data);
static void observer_fcall_end(zend_execute_data *execute_data, zval *return_value);

/* ── Stack helper: safe push with bounds check ── */

static inline void stack_push(profiler_state_t *state, size_t value)
{
    if (state->stack_depth < PROFILER_MAX_STACK) {
        state->stack[state->stack_depth++] = value;
    } else {
        /* Stack is physically full — record overflow so fcall_end can
         * skip the corresponding pop and maintain begin/end pairing. */
        state->stack_overflow_count++;
    }
}

/* ── Span creation helper ── */

static size_t create_span_entry(profiler_state_t *state, zend_execute_data *execute_data,
                                  hook_entry_t *hook)
{
    char name_buf[PROFILER_NAME_MAX];
    size_t name_len = get_function_name(execute_data, name_buf, sizeof(name_buf));

    char filepath[PROFILER_FILEPATH_MAX], function[PROFILER_FUNCNAME_MAX], ns[PROFILER_FUNCNAME_MAX];
    size_t filepath_len, function_len, ns_len;
    uint32_t lineno;
    extract_code_attrs(execute_data, filepath, sizeof(filepath), &filepath_len,
        function, sizeof(function), &function_len,
        ns, sizeof(ns), &ns_len, &lineno);

    /* Find parent span: walk backward through stack to skip sentinels */
    size_t parent_span_idx = 0;
    int has_stack_parent = 0;
    if (state->stack_depth > 0) {
        for (int i = (int)state->stack_depth - 1; i >= 0; i--) {
            if (state->stack[i] != (size_t)-1) {
                parent_span_idx = state->stack[i];
                has_stack_parent = 1;
                break;
            }
        }
    }

    uint32_t parent_frame = 0;
    if (has_stack_parent) {
        parent_frame = state->spans[parent_span_idx].frame_index;
    }

    uint32_t frame_idx = find_or_add_frame(state, name_buf, name_len, parent_frame,
        filepath, filepath_len, function, function_len, ns, ns_len, lineno);

    size_t span_idx = state->span_count++;
    profiler_span_t *span = &state->spans[span_idx];

    memcpy(span->trace_id, state->trace_id, 32);
    profiler_generate_hex_id(state, span->span_id, 16);
    span->frame_index = frame_idx;
    span->start_time_ns = realtime_ns();
    span->end_time_ns = 0;       /* not yet completed */
    span->depth = (uint32_t)state->stack_depth;
    span->kind = hook ? hook->span_kind : SPAN_KIND_INTERNAL;
    span->status_code = SPAN_STATUS_UNSET;
    span->name_override_len = 0;
    span->pre_data = NULL;
    span->skip_span = 0;
    span->exported = 0;

    if (has_stack_parent) {
        memcpy(span->parent_span_id, state->spans[parent_span_idx].span_id, 16);
        span->has_parent = 1;
    } else if (state->root.active) {
        memcpy(span->parent_span_id, state->root.span_id, 16);
        span->has_parent = 1;
    } else {
        memset(span->parent_span_id, '0', 16);
        span->has_parent = 0;
    }

    return span_idx;
}

/* ── Span undo helper ── */

static void undo_span(profiler_state_t *state, size_t span_idx)
{
    if (span_idx == state->span_count - 1) {
        state->span_count--;
        /* Remove stale attribute/event records so the next span
         * that reuses this index does not inherit them. */
        uint32_t idx = (uint32_t)span_idx;
        profiler_remove_db_attrs(state, idx);
        profiler_remove_http_attrs(state, idx);
        profiler_remove_msg_attrs(state, idx);
        profiler_remove_template_attrs(state, idx);
        profiler_remove_exception_events(state, idx);
    }
}

/* ── observer_init: called for every function call to decide if we observe it ── */

static zend_observer_fcall_handlers observer_init(zend_execute_data *execute_data)
{
    (void)execute_data;
    profiler_state_t *state = g_state;

    /* Quick bail-out: no profiling active → no handlers, zero overhead */
    if (!state || !state->active) {
        zend_observer_fcall_handlers h = {NULL, NULL};
        return h;
    }

    zend_observer_fcall_handlers h = {observer_fcall_begin, observer_fcall_end};
    return h;
}

/* ── observer_fcall_begin: called BEFORE any PHP function executes ── */

static void observer_fcall_begin(zend_execute_data *execute_data)
{
    profiler_state_t *state = g_state;

    /* Defensive: init already checked, but be safe. */
    if (!state || !state->active || state->stack_depth >= state->max_depth
        || !execute_data->func) {
        /* If profiling is active but we're at max depth or no func,
         * push a sentinel so fcall_end doesn't corrupt the stack.
         * stack_push handles physical overflow via overflow counter. */
        if (state && state->active) {
            stack_push(state, (size_t)-1);
        }
        return;
    }

    zend_function *func = execute_data->func;
    int is_userland = (func->type == ZEND_USER_FUNCTION || func->type == ZEND_EVAL_CODE);

    /* Run side-effects for internal functions (e.g. curl_setopt header tracking) */
    if (!is_userland) {
        hook_registry_run_side_effects(&g_hook_registry, state, execute_data);
    }

    /* Determine hook type and look up registered hook */
    int hook_type = is_userland ? HOOK_TYPE_USERLAND : HOOK_TYPE_INTERNAL;
    hook_entry_t *hook = hook_registry_find(&g_hook_registry, execute_data, hook_type);


    /* Filtering: only registered hooks produce spans. Unregistered calls
     * push a sentinel so fcall_end still pops cleanly. */
    if (!hook) {
        stack_push(state, (size_t)-1);
        return;
    }

    if (!ensure_span_capacity(state)) {
        stack_push(state, (size_t)-1);
        return;
    }

    size_t span_idx = create_span_entry(state, execute_data, hook);

    /* Run pre-hook (records attributes, may set skip_span) */
    if (hook && hook->pre_hook) {
        void *pre_data = hook->pre_hook(state, execute_data,
                                         &state->spans[span_idx], (uint32_t)span_idx);
        state->spans[span_idx].pre_data = pre_data;

        if (pre_data == (void *)(uintptr_t)1) {
            state->spans[span_idx].skip_span = 1;
        }
    }

    stack_push(state, span_idx);
}

/* Resolve the fate of any in-flight exception as a frame unwinds. Called for
 * every frame pop (including overflow-dropped and sentinel frames) since an
 * exception unwinds through all of them. */
static void resolve_unwinding_exception(profiler_state_t *state)
{
    if (EG(exception) != NULL && state->stack_depth == 0) {
        /* The outermost frame is unwinding with an exception still live:
         * nothing above can catch it, so it is uncaught. Promote now, before
         * the engine's fatal-error handler clears EG(exception). This records
         * the escape on the root span even when no hooked span was on the
         * stack (no pending event), so root status survives to shutdown. */
        profiler_mark_escaped_exceptions(state);
    } else if (state->exception_event_count > 0) {
        /* Otherwise drop any exception whose span has returned and is no
         * longer propagating — it was caught by application code. */
        profiler_settle_caught_exceptions(state);
    }
}

/* ── observer_fcall_end: called AFTER any PHP function returns ── */

static void observer_fcall_end(zend_execute_data *execute_data, zval *return_value)
{
    profiler_state_t *state = g_state;

    if (!state || !state->active) return;

    /* If pushes were dropped due to stack overflow, skip the corresponding
     * pop to maintain 1:1 begin/end pairing. The frame still unwound, so
     * resolve exceptions first — otherwise a throw escaping through an
     * overflow-dropped frame would never be promoted. */
    if (state->stack_overflow_count > 0) {
        state->stack_overflow_count--;
        resolve_unwinding_exception(state);
        return;
    }

    if (state->stack_depth == 0) return;

    size_t span_idx = state->stack[--state->stack_depth];

    /* Exception resolution: this frame just unwound. Runs for every frame
     * (sentinels included) since exceptions unwind through all of them. */
    resolve_unwinding_exception(state);

    /* Sentinel: this function was skipped in fcall_begin */
    if (span_idx == (size_t)-1) return;

    if (span_idx >= state->span_count) return;

    profiler_span_t *span = &state->spans[span_idx];
    span->end_time_ns = realtime_ns();

    /* Handle skip_span (pre_hook returned skip marker) */
    if (span->skip_span) {
        undo_span(state, span_idx);
        return;
    }

    /* Re-lookup the hook for threshold check */
    zend_function *func = execute_data->func;
    if (!func) return;

    int is_userland = (func->type == ZEND_USER_FUNCTION || func->type == ZEND_EVAL_CODE);
    int hook_type = is_userland ? HOOK_TYPE_USERLAND : HOOK_TYPE_INTERNAL;
    hook_entry_t *hook = hook_registry_find(&g_hook_registry, execute_data, hook_type);

    /* Per-hook threshold check */
    if (hook && hook->min_duration_ns > 0) {
        uint64_t duration = span->end_time_ns - span->start_time_ns;
        if (duration < hook->min_duration_ns) {
            undo_span(state, span_idx);
            return;
        }
    }

    /* Run post-hook */
    if (hook && hook->post_hook) {
        hook->post_hook(state, execute_data, return_value, span,
                        (uint32_t)span_idx, span->pre_data);
    }

    /* Global min_duration check */
    if (state->min_duration_ns > 0) {
        uint64_t duration = span->end_time_ns - span->start_time_ns;
        if (duration < state->min_duration_ns) {
            undo_span(state, span_idx);
            return;
        }
    }

    maybe_flush(state);
}

/* ── Observer lifecycle ── */

void observer_register(void)
{
    zend_observer_fcall_register(observer_init);
}

/* No unregister needed — observer stays registered for process lifetime.
 * When profiling is off, observer_init returns {NULL, NULL} for zero overhead. */
