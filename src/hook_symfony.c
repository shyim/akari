#include "profiler_internal.h"
#include "hook_registry.h"

/*
 * Symfony component instrumentation (beyond basic framework route detection).
 *
 * Hooks:
 *   - EventDispatcher: EventDispatcherInterface::dispatch()
 *   - HttpCache: HttpCache::handle(), lookup(), store()
 *   - HttpKernel: HttpKernel::terminate()
 *   - HttpClient: HttpClientInterface::request()
 *   - Mailer: AbstractTransport::send()
 *   - Messenger: Worker::handleMessage(), HandleMessageMiddleware::handle()
 *   - Console: Application::doRunCommand()
 *   - Cache: AbstractAdapter::get(), TagAwareAdapter::invalidateTags()
 *   - Serializer: SerializerInterface::serialize(), deserialize()
 *   - Security: AuthenticatorManager::authenticateRequest()
 *   - Notifier: NotifierInterface::send()
 *   - Process: Process::run(), Process::start()
 */

/* ── EventDispatcher hooks ──
 *
 * Symfony's dispatch signature:
 *   dispatch(object $event, string $eventName = null): object
 *
 * If $eventName (arg 2) is provided, use it.
 * Otherwise, fall back to the event object's class name (arg 1).
 *
 * Shopware decorates event_dispatcher several times. Each decorator forwards
 * the same event object/name through the chain. Trace that as one logical
 * dispatch span and keep true nested dispatches (different event object/name)
 * visible.
 */

#define EVENT_DISPATCH_CONTEXT_PUSHED ((void *)(uintptr_t)2)

static size_t clamp_event_name_len(size_t len)
{
    return len < PROFILER_EVENT_NAME_MAX ? len : PROFILER_EVENT_NAME_MAX - 1;
}

static int event_dispatch_context_matches(profiler_state_t *state,
                                            uint64_t event_handle,
                                            const char *event_name,
                                            size_t event_name_len)
{
    if (!event_handle || !event_name || event_name_len == 0) return 0;

    for (uint32_t i = state->event_dispatch_depth; i > 0; i--) {
        profiler_event_dispatch_entry_t *entry = &state->event_dispatch_stack[i - 1];
        if (entry->event_handle != event_handle) continue;
        if (entry->event_name_len != event_name_len) continue;
        if (memcmp(entry->event_name, event_name, event_name_len) == 0) {
            return 1;
        }
    }

    return 0;
}

static int event_dispatch_context_push(profiler_state_t *state,
                                         uint64_t event_handle,
                                         const char *event_name,
                                         size_t event_name_len)
{
    if (!event_handle || !event_name || event_name_len == 0) return 0;
    if (state->event_dispatch_depth >= PROFILER_EVENT_DISPATCH_STACK_MAX) return 0;

    profiler_event_dispatch_entry_t *entry =
        &state->event_dispatch_stack[state->event_dispatch_depth++];
    entry->event_handle = event_handle;
    entry->event_name_len = event_name_len;
    memcpy(entry->event_name, event_name, event_name_len);
    entry->event_name[event_name_len] = '\0';

    return 1;
}

static void *event_dispatch_pre(profiler_state_t *state, zend_execute_data *execute_data,
                                  profiler_span_t *span, uint32_t span_index)
{
    (void)span_index;

    const char *event_name = NULL;
    size_t event_name_len = 0;
    char class_buf[256] = {0};
    uint64_t event_handle = 0;

    uint32_t num_args = ZEND_CALL_NUM_ARGS(execute_data);

    /* Try arg 2: explicit event name string */
    if (num_args >= 2) {
        zval *name_arg = ZEND_CALL_ARG(execute_data, 2);
        if (name_arg && Z_TYPE_P(name_arg) == IS_STRING && Z_STRLEN_P(name_arg) > 0) {
            event_name = Z_STRVAL_P(name_arg);
            event_name_len = clamp_event_name_len(Z_STRLEN_P(name_arg));
        }
    }

    /* Fallback: use event object's class name (arg 1) */
    if (num_args >= 1) {
        zval *event_arg = ZEND_CALL_ARG(execute_data, 1);
        if (event_arg && Z_TYPE_P(event_arg) == IS_OBJECT) {
            event_handle = (uint64_t)Z_OBJ_HANDLE_P(event_arg);
            zend_class_entry *ce = Z_OBJCE_P(event_arg);
            if (!event_name && ce && ce->name) {
                snprintf(class_buf, sizeof(class_buf), "%s", ZSTR_VAL(ce->name));
                event_name = class_buf;
                event_name_len = clamp_event_name_len(strlen(class_buf));
            }
        }
    }

    if (event_dispatch_context_matches(state, event_handle, event_name, event_name_len)) {
        return HOOK_PRE_SKIP_SPAN;
    }

    if (event_name && event_name_len > 0) {
        int n = snprintf(span->name_override, SPAN_NAME_OVERRIDE_MAX,
            "event.dispatch %s", event_name);
        span->name_override_len = profiler_clamp_snprintf_len(n, sizeof(span->name_override));
    }

    if (event_dispatch_context_push(state, event_handle, event_name, event_name_len)) {
        return EVENT_DISPATCH_CONTEXT_PUSHED;
    }

    return NULL;
}

static void event_dispatch_post(profiler_state_t *state, zend_execute_data *execute_data,
                                  zval *return_value, profiler_span_t *span,
                                  uint32_t span_index, void *pre_data)
{
    (void)execute_data;
    (void)return_value;
    (void)span;
    (void)span_index;

    if (pre_data == EVENT_DISPATCH_CONTEXT_PUSHED && state->event_dispatch_depth > 0) {
        state->event_dispatch_depth--;
    }
}

/* ── Registration ── */

void hook_symfony_register(hook_registry_t *reg)
{
    /* ── EventDispatcher ── */

    hook_register_method_threshold_kind(reg,
        "Symfony\\Contracts\\EventDispatcher\\EventDispatcherInterface", "dispatch",
        HOOK_TYPE_USERLAND, SPAN_KIND_INTERNAL, 1,
        HOOK_THRESHOLD_EVENT_DISPATCH,
        event_dispatch_pre, event_dispatch_post);

    /* ── HttpKernel ── */

    /* HttpCache — Symfony reverse proxy cache */
    hook_register_method(reg,
        "Symfony\\Component\\HttpKernel\\HttpCache\\HttpCache", "handle",
        HOOK_TYPE_USERLAND, SPAN_KIND_INTERNAL, 1,
        NULL, NULL);
    hook_register_method(reg,
        "Symfony\\Component\\HttpKernel\\HttpCache\\HttpCache", "lookup",
        HOOK_TYPE_USERLAND, SPAN_KIND_INTERNAL, 1,
        NULL, NULL);
    hook_register_method(reg,
        "Symfony\\Component\\HttpKernel\\HttpCache\\HttpCache", "store",
        HOOK_TYPE_USERLAND, SPAN_KIND_INTERNAL, 1,
        NULL, NULL);

    /* Kernel::terminate — post-response work */
    hook_register_method(reg,
        "Symfony\\Component\\HttpKernel\\HttpKernel", "terminate",
        HOOK_TYPE_USERLAND, SPAN_KIND_INTERNAL, 1,
        NULL, NULL);

    /* ── HttpClient ── */

    hook_register_method(reg,
        "Symfony\\Contracts\\HttpClient\\HttpClientInterface", "request",
        HOOK_TYPE_USERLAND, SPAN_KIND_CLIENT, 1,
        NULL, NULL);

    /* ── Mailer ── */

    hook_register_method(reg,
        "Symfony\\Component\\Mailer\\Transport\\AbstractTransport", "send",
        HOOK_TYPE_USERLAND, SPAN_KIND_INTERNAL, 1,
        NULL, NULL);

    /* ── Messenger ── */

    hook_register_method(reg,
        "Symfony\\Component\\Messenger\\Worker", "handleMessage",
        HOOK_TYPE_USERLAND, SPAN_KIND_INTERNAL, 1,
        NULL, NULL);
    hook_register_method(reg,
        "Symfony\\Component\\Messenger\\Middleware\\HandleMessageMiddleware", "handle",
        HOOK_TYPE_USERLAND, SPAN_KIND_INTERNAL, 1,
        NULL, NULL);

    /* ── Console ── */

    hook_register_method(reg,
        "Symfony\\Component\\Console\\Application", "doRunCommand",
        HOOK_TYPE_USERLAND, SPAN_KIND_INTERNAL, 1,
        NULL, NULL);

    /* ── Cache ── */

    /* AbstractAdapter::get — cache fetch (cold = miss + compute + store) */
    hook_register_method(reg,
        "Symfony\\Component\\Cache\\Adapter\\AbstractAdapter", "get",
        HOOK_TYPE_USERLAND, SPAN_KIND_INTERNAL, 1,
        NULL, NULL);

    /* TagAwareAdapter::invalidateTags — cache invalidation */
    hook_register_method(reg,
        "Symfony\\Component\\Cache\\Adapter\\TagAwareAdapter", "invalidateTags",
        HOOK_TYPE_USERLAND, SPAN_KIND_INTERNAL, 1,
        NULL, NULL);

    /* ── Serializer ── */

    hook_register_method(reg,
        "Symfony\\Component\\Serializer\\SerializerInterface", "serialize",
        HOOK_TYPE_USERLAND, SPAN_KIND_INTERNAL, 1,
        NULL, NULL);
    hook_register_method(reg,
        "Symfony\\Component\\Serializer\\SerializerInterface", "deserialize",
        HOOK_TYPE_USERLAND, SPAN_KIND_INTERNAL, 1,
        NULL, NULL);

    /* ── Security ── */

    hook_register_method(reg,
        "Symfony\\Component\\Security\\Http\\Authentication\\AuthenticatorManager", "authenticateRequest",
        HOOK_TYPE_USERLAND, SPAN_KIND_INTERNAL, 1,
        NULL, NULL);

    /* ── Notifier ── */

    hook_register_method(reg,
        "Symfony\\Component\\Notifier\\NotifierInterface", "send",
        HOOK_TYPE_USERLAND, SPAN_KIND_INTERNAL, 1,
        NULL, NULL);

    /* ── Process ── */

    hook_register_method(reg,
        "Symfony\\Component\\Process\\Process", "run",
        HOOK_TYPE_USERLAND, SPAN_KIND_INTERNAL, 0,
        NULL, NULL);
    hook_register_method(reg,
        "Symfony\\Component\\Process\\Process", "start",
        HOOK_TYPE_USERLAND, SPAN_KIND_INTERNAL, 0,
        NULL, NULL);
}
