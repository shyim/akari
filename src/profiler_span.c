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
        return snprintf(buf, buf_size, "<unknown>");
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
            return snprintf(buf, buf_size, "%s::{closure:%s(%u)}", class_name, file, line);
        }
        return snprintf(buf, buf_size, "{closure:%s(%u)}", file, line);
    }

    if (class_name && func_name) {
        return snprintf(buf, buf_size, "%s::%s", class_name, func_name);
    }
    if (func_name) {
        return snprintf(buf, buf_size, "%s", func_name);
    }
    if (func->type == ZEND_USER_FUNCTION && func->op_array.filename) {
        return snprintf(buf, buf_size, "%s", ZSTR_VAL(func->op_array.filename));
    }
    return snprintf(buf, buf_size, "<main>");
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

const profiler_exception_event_t *profiler_get_exception_event(profiler_state_t *state, uint32_t span_index)
{
    if (!state || !state->exception_events) return NULL;
    for (size_t i = 0; i < state->exception_event_count; i++) {
        if (state->exception_events[i].span_index == span_index) {
            return &state->exception_events[i];
        }
    }
    return NULL;
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
    if (state->flush_fn && state->span_count >= state->flush_threshold) {
        state->flush_fn(state, state->flush_user_data);
        /* After flush callback exports, reset span buffer (keep frames) */
        state->span_count = 0;
        state->overflow_warned = 0;
    }
}
