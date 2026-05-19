#include "profiler_internal.h"
#include "hook_registry.h"

/*
 * Shopware 6 deep instrumentation — userland hooks.
 *
 * Covers the key performance-relevant layers:
 *   - Kernel / HTTP lifecycle
 *   - Data Abstraction Layer (EntityRepository search/write)
 *   - Cache layer (HTTP cache, cache invalidation)
 *   - Event system (NestedEventDispatcher, BusinessEventDispatcher)
 *   - Storefront page/pagelet loaders
 *   - Async message consumption
 *   - Flow / webhook dispatching
 */

/* ── Registration ── */

void hook_shopware_register(hook_registry_t *reg)
{
    /* ── Kernel lifecycle ── */

    /* KernelFactory::create — Shopware bootstrap */
    hook_register_method(reg,
        "Shopware\\Core\\Framework\\Adapter\\Kernel\\KernelFactory", "create",
        HOOK_TYPE_USERLAND, SPAN_KIND_INTERNAL, 0,
        NULL, NULL);

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

    /* ── Event system ── */

    /* NestedEventDispatcher — main dispatcher in Shopware */
    hook_register_method(reg,
        "Shopware\\Core\\Framework\\Event\\NestedEventDispatcher", "dispatch",
        HOOK_TYPE_USERLAND, SPAN_KIND_INTERNAL, 0,
        NULL, NULL);

    /* BusinessEventDispatcher — business event routing */
    hook_register_method(reg,
        "Shopware\\Core\\Framework\\Event\\BusinessEventDispatcher", "dispatch",
        HOOK_TYPE_USERLAND, SPAN_KIND_INTERNAL, 0,
        NULL, NULL);

    /* FlowDispatcher — automation/flow builder */
    hook_register_method(reg,
        "Shopware\\Core\\Content\\Flow\\Dispatching\\FlowDispatcher", "dispatch",
        HOOK_TYPE_USERLAND, SPAN_KIND_INTERNAL, 0,
        NULL, NULL);

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
