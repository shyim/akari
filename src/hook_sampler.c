#include "profiler_internal.h"
#include "sampler_timer.h"
#include <pthread.h>

/* ── Sampling mode: platform timer + VM interrupt ── */

static int sampler_pending = 0;
static pthread_mutex_t sampler_mutex = PTHREAD_MUTEX_INITIALIZER;
static int sampler_active = 0;
static void (*original_interrupt_function)(zend_execute_data *execute_data) = NULL;

/**
 * Called from the platform timer's handler thread.
 * Sets the pending flag and wakes the PHP VM.
 */
static void sampler_timer_callback(void *user_data)
{
    (void)user_data;
    pthread_mutex_lock(&sampler_mutex);
    __atomic_store_n(&sampler_pending, 1, __ATOMIC_RELEASE);
    pthread_mutex_unlock(&sampler_mutex);
    zend_atomic_bool_store(&EG(vm_interrupt), 1);
}

/**
 * Called from zend_interrupt_function when VM checks interrupt flag.
 * Captures a stack sample from current execute_data.
 */
static void sampler_interrupt_handler(zend_execute_data *execute_data)
{
    if (__atomic_load_n(&sampler_pending, __ATOMIC_ACQUIRE) && g_state && g_state->active) {
        __atomic_store_n(&sampler_pending, 0, __ATOMIC_RELEASE);

        profiler_state_t *state = g_state;
        uint64_t now = realtime_ns();

        if (!ensure_span_capacity(state)) goto chain;

        /* Walk the execute_data chain to build a stack sample */
        zend_execute_data *ed = execute_data;
        uint32_t parent_frame = 0;

        /* Collect user-code frames top-down, then process bottom-up */
        zend_execute_data *stack_eds[PROFILER_MAX_STACK];
        uint32_t stack_count = 0;

        while (ed && stack_count < state->max_depth && stack_count < PROFILER_MAX_STACK) {
            if (ed->func && ZEND_USER_CODE(ed->func->common.type)) {
                stack_eds[stack_count++] = ed;
            }
            ed = ed->prev_execute_data;
        }

        /* Process bottom-up (deepest caller first) to build frame chain */
        for (int i = (int)stack_count - 1; i >= 0; i--) {
            char name_buf[PROFILER_NAME_MAX];
            size_t name_len = get_function_name(stack_eds[i], name_buf, sizeof(name_buf));

            char filepath[PROFILER_FILEPATH_MAX], function[PROFILER_FUNCNAME_MAX], ns[PROFILER_FUNCNAME_MAX];
            size_t filepath_len, function_len, ns_len;
            uint32_t lineno;
            extract_code_attrs(stack_eds[i], filepath, sizeof(filepath), &filepath_len,
                function, sizeof(function), &function_len,
                ns, sizeof(ns), &ns_len, &lineno);

            parent_frame = find_or_add_frame(state, name_buf, name_len, parent_frame,
                filepath, filepath_len, function, function_len, ns, ns_len, lineno);
        }

        /* Create a single span for this sample pointing to the leaf frame */
        size_t span_idx = state->span_count++;
        profiler_span_t *span = &state->spans[span_idx];

        memcpy(span->trace_id, state->trace_id, 32);
        profiler_generate_hex_id(state, span->span_id, 16);
        span->frame_index = parent_frame;
        span->start_time_ns = now;
        span->end_time_ns = now; /* sample = point-in-time */
        span->depth = stack_count > 0 ? stack_count - 1 : 0;
        span->has_parent = 0;
        memset(span->parent_span_id, '0', 16);

        maybe_flush(state);
    }

chain:
    if (original_interrupt_function) {
        original_interrupt_function(execute_data);
    }
}

int sampler_start(double period_sec)
{
    if (period_sec <= 0) period_sec = 0.01; /* default 10ms */

    uint64_t period_ns = (uint64_t)(period_sec * 1e9);
    if (period_ns == 0) period_ns = 10000000;

    /* Hook VM interrupt */
    original_interrupt_function = zend_interrupt_function;
    zend_interrupt_function = sampler_interrupt_handler;

    if (sampler_timer_start(period_ns, sampler_timer_callback, NULL) != 0) {
        zend_interrupt_function = original_interrupt_function;
        original_interrupt_function = NULL;
        return -1;
    }

    sampler_active = 1;
    return 0;
}

void sampler_stop(void)
{
    if (sampler_active) {
        sampler_timer_stop();
        sampler_active = 0;
    }
    if (original_interrupt_function) {
        zend_interrupt_function = original_interrupt_function;
        original_interrupt_function = NULL;
    }
    __atomic_store_n(&sampler_pending, 0, __ATOMIC_RELEASE);
}
