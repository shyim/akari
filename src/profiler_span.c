#include "profiler_internal.h"

/* ── Time ── */

uint64_t realtime_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

uint64_t monotonic_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

/* ── ID generation ── */

static uint64_t xorshift64(profiler_state_t *state)
{
    if (state->rng_state == 0) {
        state->rng_state = monotonic_ns() ^ (uint64_t)(uintptr_t)state;
    }
    uint64_t x = state->rng_state;
    x ^= x << 13;
    x ^= x >> 7;
    x ^= x << 17;
    state->rng_state = x;
    return x;
}

void profiler_generate_hex_id(profiler_state_t *state, char *buf, size_t len)
{
    static const char hex[] = "0123456789abcdef";
    for (size_t i = 0; i < len; i += 2) {
        uint8_t byte = (uint8_t)xorshift64(state);
        buf[i] = hex[byte >> 4];
        buf[i + 1] = hex[byte & 0x0f];
    }
}

/* ── Function name extraction ── */

size_t get_function_name(zend_execute_data *execute_data, char *buf, size_t buf_size)
{
    if (!execute_data || !execute_data->func) {
        int n = snprintf(buf, buf_size, "<unknown>");
        return profiler_clamp_snprintf_len(n, buf_size);
    }

    zend_function *func = execute_data->func;
    const char *class_name = NULL;
    const char *func_name = NULL;

    if (func->common.scope) {
        class_name = ZSTR_VAL(func->common.scope->name);
    }
    if (func->common.function_name) {
        func_name = ZSTR_VAL(func->common.function_name);
    }

    /* Closure detection */
    if (func->type == ZEND_USER_FUNCTION && (func->op_array.fn_flags & ZEND_ACC_CLOSURE)) {
        const char *file = func->op_array.filename ? ZSTR_VAL(func->op_array.filename) : "<unknown>";
        uint32_t line = func->op_array.line_start;
        if (class_name) {
            int n = snprintf(buf, buf_size, "%s::{closure:%s(%u)}", class_name, file, line);
            return profiler_clamp_snprintf_len(n, buf_size);
        }
        int n = snprintf(buf, buf_size, "{closure:%s(%u)}", file, line);
        return profiler_clamp_snprintf_len(n, buf_size);
    }

    if (class_name && func_name) {
        int n = snprintf(buf, buf_size, "%s::%s", class_name, func_name);
        return profiler_clamp_snprintf_len(n, buf_size);
    }
    if (func_name) {
        int n = snprintf(buf, buf_size, "%s", func_name);
        return profiler_clamp_snprintf_len(n, buf_size);
    }
    if (func->type == ZEND_USER_FUNCTION && func->op_array.filename) {
        int n = snprintf(buf, buf_size, "%s", ZSTR_VAL(func->op_array.filename));
        return profiler_clamp_snprintf_len(n, buf_size);
    }
    int n = snprintf(buf, buf_size, "<main>");
    return profiler_clamp_snprintf_len(n, buf_size);
}

/* ── Frame deduplication ── */

static int ensure_frame_capacity(profiler_state_t *state)
{
    if (state->frame_count < state->frame_capacity) return 1;

    size_t new_cap = state->frame_capacity * 2;
    if (new_cap > PROFILER_MAX_FRAMES) new_cap = PROFILER_MAX_FRAMES;
    if (new_cap <= state->frame_capacity) return 0;

    profiler_frame_t *new_frames = realloc(state->frames, new_cap * sizeof(profiler_frame_t));
    if (!new_frames) return 0;
    state->frames = new_frames;
    state->frame_capacity = new_cap;
    return 1;
}

static size_t copy_truncated(char *dst, size_t dst_size, const char *src, size_t src_len)
{
    size_t len = src_len < dst_size - 1 ? src_len : dst_size - 1;
    memcpy(dst, src, len);
    dst[len] = '\0';
    return len;
}

/**
 * Extract code attributes from execute_data into provided buffers.
 */
void extract_code_attrs(zend_execute_data *execute_data,
    char *filepath, size_t filepath_size, size_t *filepath_len,
    char *function, size_t function_size, size_t *function_len,
    char *ns, size_t ns_size, size_t *ns_len,
    uint32_t *lineno)
{
    *filepath_len = 0;
    *function_len = 0;
    *ns_len = 0;
    *lineno = 0;
    filepath[0] = '\0';
    function[0] = '\0';
    ns[0] = '\0';

    if (!execute_data || !execute_data->func) return;

    zend_function *func = execute_data->func;

    /* code.function — bare function name without class */
    if (func->common.function_name) {
        const char *fn = ZSTR_VAL(func->common.function_name);
        *function_len = copy_truncated(function, function_size, fn, strlen(fn));
    }

    /* code.namespace — class name */
    if (func->common.scope) {
        const char *cn = ZSTR_VAL(func->common.scope->name);
        *ns_len = copy_truncated(ns, ns_size, cn, strlen(cn));
    }

    /* code.filepath + code.lineno */
    if (func->type == ZEND_USER_FUNCTION) {
        /* User function: use its own file and opline */
        if (func->op_array.filename) {
            const char *fp = ZSTR_VAL(func->op_array.filename);
            *filepath_len = copy_truncated(filepath, filepath_size, fp, strlen(fp));
        }
        if (execute_data->opline) {
            *lineno = execute_data->opline->lineno;
        } else {
            *lineno = func->op_array.line_start;
        }
    } else {
        /* Internal function (PDO, curl, etc.): use the caller's location.
         * Walk prev_execute_data to find the calling user code. */
        zend_execute_data *caller = execute_data->prev_execute_data;
        while (caller) {
            if (caller->func && ZEND_USER_CODE(caller->func->common.type)) {
                if (caller->func->op_array.filename) {
                    const char *fp = ZSTR_VAL(caller->func->op_array.filename);
                    *filepath_len = copy_truncated(filepath, filepath_size, fp, strlen(fp));
                }
                if (caller->opline) {
                    *lineno = caller->opline->lineno;
                } else {
                    *lineno = caller->func->op_array.line_start;
                }
                break;
            }
            caller = caller->prev_execute_data;
        }
    }
}

/**
 * Find or add a frame. Returns frame index (1-based, 0 = no frame).
 * Dedup key: (name, parent_index). Extra attrs stored on first insert.
 */
uint32_t find_or_add_frame(profiler_state_t *state,
    const char *name, size_t name_len, uint32_t parent_index,
    const char *filepath, size_t filepath_len,
    const char *function, size_t function_len,
    const char *ns, size_t ns_len,
    uint32_t lineno)
{
    /* Search existing frames */
    for (size_t i = 0; i < state->frame_count; i++) {
        if (state->frames[i].parent_index == parent_index &&
            state->frames[i].name_len == name_len &&
            memcmp(state->frames[i].name, name, name_len) == 0) {
            return (uint32_t)(i + 1); /* 1-based */
        }
    }

    /* Add new frame */
    if (!ensure_frame_capacity(state)) return 0;

    profiler_frame_t *f = &state->frames[state->frame_count];
    f->name_len = copy_truncated(f->name, PROFILER_NAME_MAX, name, name_len);
    f->filepath_len = copy_truncated(f->filepath, PROFILER_FILEPATH_MAX, filepath, filepath_len);
    f->function_len = copy_truncated(f->function, PROFILER_FUNCNAME_MAX, function, function_len);
    f->ns_len = copy_truncated(f->ns, PROFILER_FUNCNAME_MAX, ns, ns_len);
    f->lineno = lineno;
    f->parent_index = parent_index;

    state->frame_count++;
    return (uint32_t)state->frame_count; /* 1-based */
}

const profiler_frame_t *profiler_get_frame(profiler_state_t *state, uint32_t frame_index)
{
    if (frame_index == 0 || frame_index > state->frame_count) return NULL;
    return &state->frames[frame_index - 1];
}

int profiler_current_span_index(profiler_state_t *state, uint32_t *span_index)
{
    if (!state || !span_index) return 0;

    for (int i = (int)state->stack_depth - 1; i >= 0; i--) {
        size_t idx = state->stack[i];
        if (idx != (size_t)-1 && idx < state->span_count) {
            *span_index = (uint32_t)idx;
            return 1;
        }
    }

    return 0;
}

const profiler_db_attr_t *profiler_get_db_attr(profiler_state_t *state, uint32_t span_index)
{
    if (!state || !state->db_attrs) return NULL;
    for (size_t i = 0; i < state->db_attr_count; i++) {
        if (state->db_attrs[i].span_index == span_index) {
            return &state->db_attrs[i];
        }
    }
    return NULL;
}

const profiler_http_attr_t *profiler_get_http_attr(profiler_state_t *state, uint32_t span_index)
{
    if (!state || !state->http_attrs) return NULL;
    for (size_t i = 0; i < state->http_attr_count; i++) {
        if (state->http_attrs[i].span_index == span_index) {
            return &state->http_attrs[i];
        }
    }
    return NULL;
}

const profiler_messaging_attr_t *profiler_get_messaging_attr(profiler_state_t *state, uint32_t span_index)
{
    if (!state || !state->msg_attrs) return NULL;
    for (size_t i = 0; i < state->msg_attr_count; i++) {
        if (state->msg_attrs[i].span_index == span_index) {
            return &state->msg_attrs[i];
        }
    }
    return NULL;
}

const profiler_template_attr_t *profiler_get_template_attr(profiler_state_t *state, uint32_t span_index)
{
    if (!state || !state->template_attrs) return NULL;
    for (size_t i = 0; i < state->template_attr_count; i++) {
        if (state->template_attrs[i].span_index == span_index) {
            return &state->template_attrs[i];
        }
    }
    return NULL;
}

const profiler_exception_event_t *profiler_get_exception_event(profiler_state_t *state, uint32_t span_index)
{
    if (!state || !state->exception_events) return NULL;
    for (size_t i = 0; i < state->exception_event_count; i++) {
        /* Pending events are undecided (may still be caught) — never serialize
         * them. Once resolved they are either promoted (pending cleared) or
         * dropped. */
        if (state->exception_events[i].pending) continue;
        if (state->exception_events[i].span_index == span_index) {
            return &state->exception_events[i];
        }
    }
    return NULL;
}

/* True if the span still carries an undecided (pending) exception event. Such a
 * span must not be exported yet: the exception may still be caught, and an
 * exported span cannot be retracted. */
int profiler_span_has_pending_exception(profiler_state_t *state, uint32_t span_index)
{
    if (!state || !state->exception_events) return 0;
    for (size_t i = 0; i < state->exception_event_count; i++) {
        if (state->exception_events[i].pending &&
            state->exception_events[i].span_index == span_index) {
            return 1;
        }
    }
    return 0;
}

/* ── Attribute / event removal (for undo_span cleanup) ── */

void profiler_remove_db_attrs(profiler_state_t *state, uint32_t span_index)
{
    if (!state || !state->db_attrs) return;
    for (size_t i = 0; i < state->db_attr_count; i++) {
        if (state->db_attrs[i].span_index == span_index) {
            if (i + 1 < state->db_attr_count) {
                memmove(&state->db_attrs[i], &state->db_attrs[i + 1],
                        (state->db_attr_count - i - 1) * sizeof(profiler_db_attr_t));
            }
            state->db_attr_count--;
            i--; /* re-check shifted entry */
        }
    }
}

void profiler_remove_http_attrs(profiler_state_t *state, uint32_t span_index)
{
    if (!state || !state->http_attrs) return;
    for (size_t i = 0; i < state->http_attr_count; i++) {
        if (state->http_attrs[i].span_index == span_index) {
            if (i + 1 < state->http_attr_count) {
                memmove(&state->http_attrs[i], &state->http_attrs[i + 1],
                        (state->http_attr_count - i - 1) * sizeof(profiler_http_attr_t));
            }
            state->http_attr_count--;
            i--;
        }
    }
}

void profiler_remove_msg_attrs(profiler_state_t *state, uint32_t span_index)
{
    if (!state || !state->msg_attrs) return;
    for (size_t i = 0; i < state->msg_attr_count; i++) {
        if (state->msg_attrs[i].span_index == span_index) {
            if (i + 1 < state->msg_attr_count) {
                memmove(&state->msg_attrs[i], &state->msg_attrs[i + 1],
                        (state->msg_attr_count - i - 1) * sizeof(profiler_messaging_attr_t));
            }
            state->msg_attr_count--;
            i--;
        }
    }
}

void profiler_remove_template_attrs(profiler_state_t *state, uint32_t span_index)
{
    if (!state || !state->template_attrs) return;
    for (size_t i = 0; i < state->template_attr_count; i++) {
        if (state->template_attrs[i].span_index == span_index) {
            if (i + 1 < state->template_attr_count) {
                memmove(&state->template_attrs[i], &state->template_attrs[i + 1],
                        (state->template_attr_count - i - 1) * sizeof(profiler_template_attr_t));
            }
            state->template_attr_count--;
            i--;
        }
    }
}

/* Remove exception_events[i] by shifting the tail down. Caller does `i--` after
 * to re-examine the slot that moved into position i. */
static void remove_exception_event_at(profiler_state_t *state, size_t i)
{
    if (i + 1 < state->exception_event_count) {
        memmove(&state->exception_events[i], &state->exception_events[i + 1],
                (state->exception_event_count - i - 1) * sizeof(profiler_exception_event_t));
    }
    state->exception_event_count--;
}

void profiler_remove_exception_events(profiler_state_t *state, uint32_t span_index)
{
    if (!state || !state->exception_events) return;
    for (size_t i = 0; i < state->exception_event_count; i++) {
        if (state->exception_events[i].span_index == span_index) {
            remove_exception_event_at(state, i);
            i--;
        }
    }
}

/* Promote a single pending event to an escaped (uncaught) error: mark its
 * span and the root span ERROR, then clear the pending flag so it exports. */
static void promote_escaped_event(profiler_state_t *state, profiler_exception_event_t *ev)
{
    ev->pending = 0;
    if (ev->span_index < state->span_count) {
        state->spans[ev->span_index].status_code = SPAN_STATUS_ERROR;
    }
    state->root.status_code = SPAN_STATUS_ERROR;
    if (ev->exception_message[0]) {
        snprintf(state->root.status_message, sizeof(state->root.status_message),
                 "%s", ev->exception_message);
    }
}

void profiler_settle_caught_exceptions(profiler_state_t *state)
{
    if (!state || !state->exception_events) return;

    /* While an exception is still propagating its fate is undecided — an outer
     * frame may catch it, or it escapes. Leave pending events untouched. */
    if (EG(exception) != NULL) return;

    /* No exception is live, so every pending throw has stopped propagating and
     * was therefore caught and handled by application code — drop them all.
     *
     * We must not key this on whether the recording span has returned: a span
     * that catches one exception and later throws another stays open across
     * both, yet the first throw is already settled. Keeping its event pending
     * would leave a raw zend_object* in exception_obj after the engine frees
     * it; if that address is reused by the later (uncaught) exception,
     * profiler_mark_escaped_exceptions would promote the wrong event and
     * export an incorrect error. */
    for (size_t i = 0; i < state->exception_event_count; i++) {
        profiler_exception_event_t *ev = &state->exception_events[i];
        if (!ev->pending) continue;

        remove_exception_event_at(state, i);
        i--;
    }
}

void profiler_mark_escaped_exceptions(profiler_state_t *state)
{
    if (!state) return;

    /* The exception currently propagating out of the outermost frame is the
     * one that escaped. */
    zend_object *live = EG(exception);

    /* exit()/die() unwinds via the same EG(exception) machinery but is not an
     * error — the engine itself distinguishes these sentinels. Treating them as
     * escaped exceptions would mark every CLI command that ends in exit($code)
     * (Symfony console, Laravel artisan, …) as a failed span. Ignore them. */
    if (live && (zend_is_unwind_exit(live) || zend_is_graceful_exit(live))) {
        live = NULL;
    }

    /* Record the escape on the root span independently of hooked spans. The
     * engine clears EG(exception) before request shutdown, so finalize_root_span
     * cannot re-derive this; capturing it here keeps the root ERROR status even
     * when the throw happened with no hooked span on the stack. */
    if (live) {
        state->root_exception_escaped = 1;
        if (state->root.active) {
            state->root.status_code = SPAN_STATUS_ERROR;
        }
        zval rv;
        zval *msg = zend_read_property_ex(live->ce, live,
                        ZSTR_KNOWN(ZEND_STR_MESSAGE), 1, &rv);
        if (msg && Z_TYPE_P(msg) == IS_STRING) {
            snprintf(state->root_exception_message, sizeof(state->root_exception_message),
                     "%s", Z_STRVAL_P(msg));
        }
    }

    if (!state->exception_events) return;

    /* Any still-pending event whose object matches the escaping exception is
     * promoted to an error on its span. Any other still-pending event was
     * caught earlier in the same span (a try/catch that recovered, before a
     * later throw), or there is no live exception to match — drop those. */
    for (size_t i = 0; i < state->exception_event_count; i++) {
        profiler_exception_event_t *ev = &state->exception_events[i];
        if (!ev->pending) continue;

        if (live && ev->exception_obj == (void *)live) {
            promote_escaped_event(state, ev);
        } else {
            remove_exception_event_at(state, i);
            i--;
        }
    }
}

static int remap_span_index(uint32_t old_index, const size_t *remap,
                            size_t old_count, uint32_t *new_index)
{
    if ((size_t)old_index >= old_count) return 0;
    if (remap[old_index] == SIZE_MAX) return 0;
    *new_index = (uint32_t)remap[old_index];
    return 1;
}

void profiler_compact_exported_spans(profiler_state_t *state)
{
    if (!state || state->span_count == 0) return;

    size_t old_count = state->span_count;
    size_t *remap = malloc(old_count * sizeof(size_t));
    if (!remap) return;

    size_t new_count = 0;
    for (size_t i = 0; i < old_count; i++) {
        if (state->spans[i].exported && state->spans[i].end_time_ns > 0) {
            remap[i] = SIZE_MAX;
            continue;
        }
        remap[i] = new_count;
        if (new_count != i) {
            state->spans[new_count] = state->spans[i];
        }
        new_count++;
    }

    if (new_count == old_count) {
        free(remap);
        return;
    }

    for (size_t i = 0; i < state->stack_depth; i++) {
        size_t old_index = state->stack[i];
        if (old_index == (size_t)-1) continue;
        if (old_index < old_count && remap[old_index] != SIZE_MAX) {
            state->stack[i] = remap[old_index];
        } else {
            state->stack[i] = (size_t)-1;
        }
    }

    int manual_count = 0;
    for (int i = 0; i < state->manual_span_count; i++) {
        size_t old_index = (size_t)state->manual_spans[i];
        if (old_index < old_count && remap[old_index] != SIZE_MAX) {
            state->manual_spans[manual_count++] = (int)remap[old_index];
        }
    }
    state->manual_span_count = manual_count;

    size_t write_idx = 0;
    if (state->db_attrs) {
        for (size_t i = 0; i < state->db_attr_count; i++) {
            uint32_t new_index;
            if (remap_span_index(state->db_attrs[i].span_index, remap, old_count, &new_index)) {
                state->db_attrs[write_idx] = state->db_attrs[i];
                state->db_attrs[write_idx].span_index = new_index;
                write_idx++;
            }
        }
    }
    state->db_attr_count = write_idx;

    write_idx = 0;
    if (state->http_attrs) {
        for (size_t i = 0; i < state->http_attr_count; i++) {
            uint32_t new_index;
            if (remap_span_index(state->http_attrs[i].span_index, remap, old_count, &new_index)) {
                state->http_attrs[write_idx] = state->http_attrs[i];
                state->http_attrs[write_idx].span_index = new_index;
                write_idx++;
            }
        }
    }
    state->http_attr_count = write_idx;

    write_idx = 0;
    if (state->msg_attrs) {
        for (size_t i = 0; i < state->msg_attr_count; i++) {
            uint32_t new_index;
            if (remap_span_index(state->msg_attrs[i].span_index, remap, old_count, &new_index)) {
                state->msg_attrs[write_idx] = state->msg_attrs[i];
                state->msg_attrs[write_idx].span_index = new_index;
                write_idx++;
            }
        }
    }
    state->msg_attr_count = write_idx;

    write_idx = 0;
    if (state->template_attrs) {
        for (size_t i = 0; i < state->template_attr_count; i++) {
            uint32_t new_index;
            if (remap_span_index(state->template_attrs[i].span_index, remap, old_count, &new_index)) {
                state->template_attrs[write_idx] = state->template_attrs[i];
                state->template_attrs[write_idx].span_index = new_index;
                write_idx++;
            }
        }
    }
    state->template_attr_count = write_idx;

    write_idx = 0;
    if (state->exception_events) {
        for (size_t i = 0; i < state->exception_event_count; i++) {
            uint32_t new_index;
            if (remap_span_index(state->exception_events[i].span_index, remap, old_count, &new_index)) {
                state->exception_events[write_idx] = state->exception_events[i];
                state->exception_events[write_idx].span_index = new_index;
                write_idx++;
            }
        }
    }
    state->exception_event_count = write_idx;

    state->span_count = new_count;
    free(remap);
}

/* ── Span capacity management ── */

int ensure_span_capacity(profiler_state_t *state)
{
    if (state->span_count < state->span_capacity) return 1;

    size_t new_cap = state->span_capacity * 2;
    if (new_cap > PROFILER_MAX_SPANS) {
        if (!state->overflow_warned) {
            state->overflow_warned = 1;
            php_error_docref(NULL, E_WARNING,
                "profiler_otel: span limit reached (%d), further spans dropped", PROFILER_MAX_SPANS);
        }
        return 0;
    }

    profiler_span_t *new_spans = realloc(state->spans, new_cap * sizeof(profiler_span_t));
    if (!new_spans) {
        if (!state->overflow_warned) {
            state->overflow_warned = 1;
            php_error_docref(NULL, E_WARNING, "profiler_otel: failed to allocate memory for spans");
        }
        return 0;
    }
    state->spans = new_spans;
    state->span_capacity = new_cap;
    return 1;
}

/* ── Flush logic ── */

void maybe_flush(profiler_state_t *state)
{
    if (!state->flush_fn) return;

    /* Count unexported, completed spans to check threshold */
    size_t pending = 0;
    for (size_t i = 0; i < state->span_count; i++) {
        if (!state->spans[i].exported && state->spans[i].end_time_ns > 0) {
            pending++;
        }
    }
    if (pending < state->flush_threshold) return;

    state->flush_fn(state, state->flush_user_data);
    /* Spans are marked exported inside udp_export_spans as they are serialized.
     * Open spans (end_time_ns == 0) remain unexported and will be picked up
     * by a later flush or the final RSHUTDOWN export. */
    profiler_compact_exported_spans(state);
    state->overflow_warned = 0;
}

/* Drop already-sent log records (indices < log_records_sent) by shifting the
 * unsent tail down. Unlike spans, log records are not referenced by index
 * elsewhere, so no remap is needed. */
void profiler_compact_exported_logs(profiler_state_t *state)
{
    if (!state || state->log_records_sent == 0) return;

    size_t sent = state->log_records_sent;
    if (sent > state->log_record_count) sent = state->log_record_count;

    size_t remaining = state->log_record_count - sent;
    if (remaining > 0) {
        memmove(&state->log_records[0], &state->log_records[sent],
                remaining * sizeof(profiler_log_record_t));
    }
    state->log_record_count = remaining;
    state->log_records_sent = 0;
}

/* Flush + compact buffered log records once enough have accumulated, bounding
 * resident memory across a long-running request. Called from Akari\log() after
 * a record is appended. */
void maybe_flush_logs(profiler_state_t *state)
{
    if (!state->flush_fn) return;

    size_t unsent = (state->log_record_count > state->log_records_sent)
        ? state->log_record_count - state->log_records_sent : 0;
    if (unsent < PROFILER_LOG_FLUSH_THRESHOLD) return;

    state->flush_fn(state, state->flush_user_data);
    /* udp_export_logs advances log_records_sent for the records it sent; drop
     * those so the buffer does not grow without bound. */
    profiler_compact_exported_logs(state);
}
