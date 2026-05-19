#include "profiler_internal.h"
#include "hook_registry.h"

/*
 * Error/exception handler instrumentation.
 *
 * Hooks framework error handlers to capture exceptions that are caught
 * and handled by the framework (not just uncaught ones). Records exception
 * class + message as a span event on the error handler's span.
 *
 * Hooks:
 *   Symfony:  ErrorHandler::handleException($exception)
 *   Shopware: no separate error handler — Shopware uses Symfony's ErrorHandler
 *             but we also hook the Shopware-specific error response paths
 *
 * The approach:
 *   - Pre-hook: extract exception from the first argument
 *   - Record an exception event with type + message
 *   - Set the span's status_code to ERROR
 *   - Also set the root span status to ERROR (the request failed)
 */

/* ── Helper: record exception event on a span ── */

static void record_exception_event(profiler_state_t *state, uint32_t span_index,
                                     zval *exception_zval)
{
    if (!exception_zval || Z_TYPE_P(exception_zval) != IS_OBJECT) return;

    /* Ensure capacity */
    if (state->exception_event_count >= state->exception_event_capacity) {
        size_t new_cap = state->exception_event_capacity
            ? state->exception_event_capacity * 2
            : PROFILER_INITIAL_EXCEPTION_EVENTS;
        profiler_exception_event_t *new_events = realloc(state->exception_events,
            new_cap * sizeof(profiler_exception_event_t));
        if (!new_events) return;
        state->exception_events = new_events;
        state->exception_event_capacity = new_cap;
    }

    profiler_exception_event_t *ev = &state->exception_events[state->exception_event_count];
    memset(ev, 0, sizeof(profiler_exception_event_t));
    ev->span_index = span_index;
    ev->timestamp_ns = realtime_ns();

    /* exception.type = class name */
    zend_class_entry *ce = Z_OBJCE_P(exception_zval);
    if (ce && ce->name) {
        snprintf(ev->exception_type, EXCEPTION_TYPE_MAX, "%s", ZSTR_VAL(ce->name));
    }

    /* exception.message = $exception->getMessage() */
    zval rv;
    zval *msg = zend_read_property_ex(ce, Z_OBJ_P(exception_zval),
                    ZSTR_KNOWN(ZEND_STR_MESSAGE), 1, &rv);
    if (msg && Z_TYPE_P(msg) == IS_STRING) {
        size_t len = Z_STRLEN_P(msg);
        if (len >= EXCEPTION_MESSAGE_MAX) len = EXCEPTION_MESSAGE_MAX - 1;
        memcpy(ev->exception_message, Z_STRVAL_P(msg), len);
        ev->exception_message[len] = '\0';
    }

    state->exception_event_count++;
}

/* ── Symfony ErrorHandler::handleException ── */

static void *symfony_handle_exception_pre(profiler_state_t *state,
                                            zend_execute_data *execute_data,
                                            profiler_span_t *span,
                                            uint32_t span_index)
{
    span->status_code = SPAN_STATUS_ERROR;

    /* First argument is the \Throwable */
    uint32_t num_args = ZEND_CALL_NUM_ARGS(execute_data);
    if (num_args >= 1) {
        zval *exception = ZEND_CALL_ARG(execute_data, 1);
        record_exception_event(state, span_index, exception);

        /* Also set root span to ERROR */
        if (state->root.active) {
            state->root.status_code = SPAN_STATUS_ERROR;
            if (exception && Z_TYPE_P(exception) == IS_OBJECT) {
                zval rv;
                zval *msg = zend_read_property_ex(Z_OBJCE_P(exception), Z_OBJ_P(exception),
                                ZSTR_KNOWN(ZEND_STR_MESSAGE), 1, &rv);
                if (msg && Z_TYPE_P(msg) == IS_STRING) {
                    snprintf(state->root.status_message, sizeof(state->root.status_message),
                             "%s", Z_STRVAL_P(msg));
                }
            }
        }
    }

    return NULL;
}

/* ── Generic exception handler pre-hook (reusable) ──
 * For handlers where the first argument is a \Throwable.
 */

static void *generic_exception_pre(profiler_state_t *state,
                                     zend_execute_data *execute_data,
                                     profiler_span_t *span,
                                     uint32_t span_index)
{
    span->status_code = SPAN_STATUS_ERROR;

    uint32_t num_args = ZEND_CALL_NUM_ARGS(execute_data);
    if (num_args >= 1) {
        zval *exception = ZEND_CALL_ARG(execute_data, 1);
        record_exception_event(state, span_index, exception);
    }

    return NULL;
}

/* ── Registration ── */

void hook_error_register(hook_registry_t *reg)
{
    /* Symfony ErrorHandler — catches all exceptions in Symfony/Shopware apps */
    hook_register_method(reg,
        "Symfony\\Component\\ErrorHandler\\ErrorHandler", "handleException",
        HOOK_TYPE_USERLAND, SPAN_KIND_INTERNAL, 1,
        symfony_handle_exception_pre, NULL);

    /* Symfony HttpKernel — exception handling within kernel */
    hook_register_method(reg,
        "Symfony\\Component\\HttpKernel\\HttpKernel", "handleThrowable",
        HOOK_TYPE_USERLAND, SPAN_KIND_INTERNAL, 1,
        generic_exception_pre, NULL);

    /* Shopware: StorefrontController uses Symfony's ErrorHandler.
     * The ApiExceptionHandler produces error responses for API requests. */
    hook_register_method(reg,
        "Shopware\\Core\\Framework\\Api\\EventListener\\ErrorResponseFactory", "getResponseFromException",
        HOOK_TYPE_USERLAND, SPAN_KIND_INTERNAL, 0,
        generic_exception_pre, NULL);

    /* Shopware: the Profiler class wraps errors for dev toolbar — trace it too */
    hook_register_method(reg,
        "Shopware\\Core\\Profiling\\Profiler", "trace",
        HOOK_TYPE_USERLAND, SPAN_KIND_INTERNAL, 0,
        NULL, NULL);
}
