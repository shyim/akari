#include "profiler_internal.h"
#include "hook_registry.h"

/*
 * Laravel component instrumentation (beyond basic route detection in hook_framework.c).
 *
 * Hooks:
 *   - Queue: Worker::process() — job processing
 *   - Events: Dispatcher::dispatch() — event system
 *   - Blade: BladeCompiler::compile() — template compilation
 *   - Eloquent: Builder::getModels() — query builder model hydration
 */

/* ── Registration ── */

void hook_laravel_register(hook_registry_t *reg)
{
    /* Queue worker — each job processed */
    hook_register_method(reg,
        "Illuminate\\Queue\\Worker", "process",
        HOOK_TYPE_USERLAND, SPAN_KIND_INTERNAL, 1,
        NULL, NULL);

    /* Event dispatcher — all event dispatches */
    hook_register_method(reg,
        "Illuminate\\Events\\Dispatcher", "dispatch",
        HOOK_TYPE_USERLAND, SPAN_KIND_INTERNAL, 1,
        NULL, NULL);

    /* Blade template compilation */
    hook_register_method(reg,
        "Illuminate\\View\\Compilers\\BladeCompiler", "compile",
        HOOK_TYPE_USERLAND, SPAN_KIND_INTERNAL, 1,
        NULL, NULL);

    /* View engine — template rendering */
    hook_register_method(reg,
        "Illuminate\\View\\Engines\\CompilerEngine", "get",
        HOOK_TYPE_USERLAND, SPAN_KIND_INTERNAL, 1,
        NULL, NULL);

    /* Eloquent query builder — model loading */
    hook_register_method(reg,
        "Illuminate\\Database\\Eloquent\\Builder", "getModels",
        HOOK_TYPE_USERLAND, SPAN_KIND_INTERNAL, 1,
        NULL, NULL);
}
