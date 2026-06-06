#include "profiler_internal.h"
#include "hook_registry.h"

/*
 * Shopware 6 deep instrumentation — userland hooks.
 *
 * Covers the key performance-relevant layers:
 *   - HTTP lifecycle
 *   - Data Abstraction Layer (EntityRepository search/write)
 *   - Cache layer (HTTP cache, cache invalidation)
 *   - Event system (NestedEventDispatcher, BusinessEventDispatcher)
 *   - Storefront page/pagelet loaders
 *   - Async message consumption
 *   - Flow / webhook dispatching
 */

/* ── Registration ── */

static void *shopware_profiler_trace_pre(profiler_state_t *state,
                                           zend_execute_data *execute_data,
                                           profiler_span_t *span,
                                           uint32_t span_index)
{
    (void)state;
    (void)span_index;

    uint32_t num_args = ZEND_CALL_NUM_ARGS(execute_data);
    if (num_args < 1) return NULL;

    zval *name_arg = ZEND_CALL_ARG(execute_data, 1);
    if (!name_arg || Z_TYPE_P(name_arg) != IS_STRING || Z_STRLEN_P(name_arg) == 0) {
        return NULL;
    }

    int n = snprintf(span->name_override, SPAN_NAME_OVERRIDE_MAX,
        "shopware.profiler %s", Z_STRVAL_P(name_arg));
    span->name_override_len = profiler_clamp_snprintf_len(n, sizeof(span->name_override));

    return NULL;
}

void hook_shopware_register(hook_registry_t *reg)
{
    /* ── Kernel lifecycle ── */

    /* HttpKernel::handle — the main Shopware request handler */
    hook_register_method(reg,
        "Shopware\\Core\\HttpKernel", "handle",
        HOOK_TYPE_USERLAND, SPAN_KIND_INTERNAL, 1,
        NULL, NULL);

    /* ── Data Abstraction Layer ── */

    /* EntityRepository::search — the primary read path */
    hook_register_method(reg,
        "Shopware\\Core\\Framework\\DataAbstractionLayer\\EntityRepository", "search",
        HOOK_TYPE_USERLAND, SPAN_KIND_INTERNAL, 1,
        NULL, NULL);

    /* EntityRepository::aggregate — aggregation queries */
    hook_register_method(reg,
        "Shopware\\Core\\Framework\\DataAbstractionLayer\\EntityRepository", "aggregate",
        HOOK_TYPE_USERLAND, SPAN_KIND_INTERNAL, 1,
        NULL, NULL);

    /* EntityRepository::upsert — write operations */
    hook_register_method(reg,
        "Shopware\\Core\\Framework\\DataAbstractionLayer\\EntityRepository", "upsert",
        HOOK_TYPE_USERLAND, SPAN_KIND_INTERNAL, 1,
        NULL, NULL);

    /* EntityRepository::create */
    hook_register_method(reg,
        "Shopware\\Core\\Framework\\DataAbstractionLayer\\EntityRepository", "create",
        HOOK_TYPE_USERLAND, SPAN_KIND_INTERNAL, 1,
        NULL, NULL);

    /* EntityRepository::update */
    hook_register_method(reg,
        "Shopware\\Core\\Framework\\DataAbstractionLayer\\EntityRepository", "update",
        HOOK_TYPE_USERLAND, SPAN_KIND_INTERNAL, 1,
        NULL, NULL);

    /* EntityRepository::delete */
    hook_register_method(reg,
        "Shopware\\Core\\Framework\\DataAbstractionLayer\\EntityRepository", "delete",
        HOOK_TYPE_USERLAND, SPAN_KIND_INTERNAL, 1,
        NULL, NULL);

    /* SalesChannelRepository::search — storefront-specific reads */
    hook_register_method(reg,
        "Shopware\\Core\\System\\SalesChannel\\Entity\\SalesChannelRepository", "search",
        HOOK_TYPE_USERLAND, SPAN_KIND_INTERNAL, 1,
        NULL, NULL);

    /* SalesChannelRepository::aggregate */
    hook_register_method(reg,
        "Shopware\\Core\\System\\SalesChannel\\Entity\\SalesChannelRepository", "aggregate",
        HOOK_TYPE_USERLAND, SPAN_KIND_INTERNAL, 1,
        NULL, NULL);

    /* EntityReader — low-level DAL read */
    hook_register_method(reg,
        "Shopware\\Core\\Framework\\DataAbstractionLayer\\Dbal\\EntityReader", "read",
        HOOK_TYPE_USERLAND, SPAN_KIND_INTERNAL, 1,
        NULL, NULL);

    /* EntityWriter — low-level DAL write */
    hook_register_method(reg,
        "Shopware\\Core\\Framework\\DataAbstractionLayer\\Write\\EntityWriter", "upsert",
        HOOK_TYPE_USERLAND, SPAN_KIND_INTERNAL, 1,
        NULL, NULL);

    /* ── Cache ── */

    /* CacheInvalidator — cache tag invalidation */
    hook_register_method(reg,
        "Shopware\\Core\\Framework\\Adapter\\Cache\\CacheInvalidator", "invalidate",
        HOOK_TYPE_USERLAND, SPAN_KIND_INTERNAL, 1,
        NULL, NULL);

    /* ── Event system ──
     *
     * Shopware decorates Symfony's event_dispatcher. Those dispatch() methods
     * are traced by the Symfony EventDispatcherInterface hook as one logical
     * event dispatch span, with duplicate decorator hops collapsed there.
     */

    /* Profiler::trace($name, $closure) — Shopware wraps expensive blocks here. */
    hook_register_method(reg,
        "Shopware\\Core\\Profiling\\Profiler", "trace",
        HOOK_TYPE_USERLAND, SPAN_KIND_INTERNAL, 0,
        shopware_profiler_trace_pre, NULL);

    /* ── Storefront page loaders ── */

    hook_register_method(reg,
        "Shopware\\Storefront\\Page\\Navigation\\NavigationPageLoader", "load",
        HOOK_TYPE_USERLAND, SPAN_KIND_INTERNAL, 0,
        NULL, NULL);

    hook_register_method(reg,
        "Shopware\\Storefront\\Page\\Product\\Detail\\ProductDetailPageLoader", "load",
        HOOK_TYPE_USERLAND, SPAN_KIND_INTERNAL, 0,
        NULL, NULL);

    hook_register_method(reg,
        "Shopware\\Storefront\\Page\\GenericPageLoader", "load",
        HOOK_TYPE_USERLAND, SPAN_KIND_INTERNAL, 0,
        NULL, NULL);

    hook_register_method(reg,
        "Shopware\\Storefront\\Pagelet\\Header\\HeaderPageletLoader", "load",
        HOOK_TYPE_USERLAND, SPAN_KIND_INTERNAL, 0,
        NULL, NULL);

    hook_register_method(reg,
        "Shopware\\Storefront\\Pagelet\\Footer\\FooterPageletLoader", "load",
        HOOK_TYPE_USERLAND, SPAN_KIND_INTERNAL, 0,
        NULL, NULL);

    /* ── Navigation ── */

    hook_register_method(reg,
        "Shopware\\Core\\Content\\Category\\Service\\NavigationLoader", "load",
        HOOK_TYPE_USERLAND, SPAN_KIND_INTERNAL, 1,
        NULL, NULL);
}
