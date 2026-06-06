#ifndef HOOK_REGISTRY_H
#define HOOK_REGISTRY_H

/*
 * Hook registry — modular callback registration for function/method instrumentation.
 *
 * Modular callback registration for function/method instrumentation.
 * Each hook file registers its own callbacks; the execute hooks just do a lookup.
 *
 * Two types of hooks:
 *   1. Internal function hooks — for zend_execute_internal (PDO, curl, Redis, mysqli, etc.)
 *   2. Userland method hooks — for zend_execute_ex (framework dispatch, Twig, etc.)
 *
 * Hooks are keyed by (lowercase_class_name, lowercase_method_name) for methods,
 * or (NULL, lowercase_function_name) for plain functions.
 *
 * Wildcard matching: set method_name to "*" and provide a method_filter callback
 * that returns 1 if the specific method should be instrumented (e.g. Redis commands).
 */

#include "profiler.h"
#include "Zend/zend_execute.h"

/* ── Hook types ── */

#define HOOK_TYPE_INTERNAL  1  /* Hooks into zend_execute_internal */
#define HOOK_TYPE_USERLAND  2  /* Hooks into zend_execute_ex */

/* Dynamic duration threshold sources. */
#define HOOK_THRESHOLD_NONE            0
#define HOOK_THRESHOLD_EVENT_DISPATCH  1

/* Pre-hook return marker: do not create a span for this call. */
#define HOOK_PRE_SKIP_SPAN ((void *)(uintptr_t)1)

/* ── Callback signatures ── */

/*
 * Pre-hook: called before the original function executes.
 * Can modify span attributes, inject headers, etc.
 * Returns opaque pointer passed to post_hook (e.g. injected curl_slist).
 */
typedef void *(*hook_pre_fn)(profiler_state_t *state,
                              zend_execute_data *execute_data,
                              profiler_span_t *span,
                              uint32_t span_index);

/*
 * Post-hook: called after the original function returns.
 * Can record response attributes, restore state, etc.
 * pre_data is whatever pre_hook returned.
 */
typedef void (*hook_post_fn)(profiler_state_t *state,
                              zend_execute_data *execute_data,
                              zval *return_value,
                              profiler_span_t *span,
                              uint32_t span_index,
                              void *pre_data);

/*
 * Side-effect hook: called for functions we need to intercept but don't
 * create spans for (e.g. curl_setopt to track user headers).
 */
typedef void (*hook_side_fn)(profiler_state_t *state,
                              zend_execute_data *execute_data);

/*
 * Method filter: for wildcard class hooks, decides if a specific method matches.
 * Returns 1 to instrument, 0 to skip.
 */
typedef int (*hook_method_filter_fn)(const char *method_name);

/* ── Hook entry ── */

typedef struct {
    /* Key: lowercase class + method. class_name="" for plain functions.
     * method_name="*" means wildcard (use method_filter). */
    char class_name[256];
    char method_name[128];
    int class_name_len;
    int method_name_len;

    /* Class-entry resolution is cached PER THREAD, not here: a zend_class_entry
     * resolved on one thread is not valid on another under ZTS, and the cache
     * is reset every request. The cache lives in hook_ce_cache_t (module
     * globals); this struct holds only immutable, MINIT-time configuration so
     * the registry itself can be shared read-only across threads. */
    int use_instanceof;       /* 1 = match subclasses via instanceof_function */

    /* Hook type */
    int hook_type;            /* HOOK_TYPE_INTERNAL or HOOK_TYPE_USERLAND */
    uint8_t span_kind;        /* SPAN_KIND_* for the created span */

    /* Wildcard filter (only used when method_name is "*") */
    hook_method_filter_fn method_filter;

    /* Per-hook minimum duration: if > 0, calls faster than this are not traced.
     * The executor times the call cheaply and only creates a full span if it
     * exceeded this threshold. Avoids overhead for high-frequency fast calls
     * like preg_match, file_get_contents (local), etc. */
    uint64_t min_duration_ns;
    uint8_t threshold_kind;

    /* Callbacks */
    hook_pre_fn pre_hook;
    hook_post_fn post_hook;
} hook_entry_t;

typedef struct {
    /* Side-effect-only entry (no span created) */
    char class_name[256];
    char method_name[128];
    int class_name_len;
    int method_name_len;
    hook_side_fn callback;
} hook_side_entry_t;

/* ── Registry ── */

#define HOOK_REGISTRY_MAX 256
#define HOOK_WILDCARD_MAX  16
#define HOOK_SIDE_MAX      16

typedef struct {
    /* Exact-match entries (stored in array + hash table for O(1) lookup) */
    hook_entry_t entries[HOOK_REGISTRY_MAX];
    int count;

    /* Wildcard class entries (small array, linear scan — typically 4-6) */
    hook_entry_t wildcards[HOOK_WILDCARD_MAX];
    int wildcard_count;

    /* Side-effect entries (hash table for O(1) lookup) */
    hook_side_entry_t side_entries[HOOK_SIDE_MAX];
    int side_count;

    /* Hash tables for O(1) dispatch (keyed by "type:class::method" or "type:function") */
    HashTable *hook_map;
    HashTable *side_map;
} hook_registry_t;

/* ── Per-thread, per-request resolved-class-entry cache ──
 *
 * Memoizes zend_lookup_class()/instanceof results for the registry's exact and
 * wildcard entries, indexed by their position in the shared registry. Lives in
 * module globals (per-thread under ZTS) and is cleared each request via
 * hook_ce_cache_reset(), since class entries are per-request and per-thread. */
/* Struct tag matches the forward declaration in php_akari.h
 * (struct hook_ce_cache_t_fwd) so the module-global pointer is the same type. */
typedef struct hook_ce_cache_t_fwd {
    zend_class_entry *exact_ce[HOOK_REGISTRY_MAX];
    uint8_t           exact_attempted[HOOK_REGISTRY_MAX];
    zend_class_entry *wildcard_ce[HOOK_WILDCARD_MAX];
    uint8_t           wildcard_attempted[HOOK_WILDCARD_MAX];
} hook_ce_cache_t;

void hook_ce_cache_reset(hook_ce_cache_t *cache);

/* ── API ── */

void hook_registry_init(hook_registry_t *reg);
void hook_registry_reset(hook_registry_t *reg);
void hook_registry_destroy(hook_registry_t *reg);

/*
 * Register a class method hook.
 * class_name: fully qualified (e.g. "PDO", "Symfony\\Component\\HttpKernel\\HttpKernelInterface")
 * method_name: e.g. "query", "handle", or "*" for wildcard with filter
 * use_instanceof: 1 to match subclasses
 */
void hook_register_method(hook_registry_t *reg,
                           const char *class_name,
                           const char *method_name,
                           int hook_type,
                           uint8_t span_kind,
                           int use_instanceof,
                           hook_pre_fn pre_hook,
                           hook_post_fn post_hook);

/*
 * Register a class method hook with a dynamic per-request threshold source.
 */
void hook_register_method_threshold_kind(hook_registry_t *reg,
                           const char *class_name,
                           const char *method_name,
                           int hook_type,
                           uint8_t span_kind,
                           int use_instanceof,
                           uint8_t threshold_kind,
                           hook_pre_fn pre_hook,
                           hook_post_fn post_hook);

/*
 * Register a class wildcard hook (matches any method that passes the filter).
 */
void hook_register_class_wildcard(hook_registry_t *reg,
                                   const char *class_name,
                                   int hook_type,
                                   uint8_t span_kind,
                                   int use_instanceof,
                                   hook_method_filter_fn filter,
                                   hook_pre_fn pre_hook,
                                   hook_post_fn post_hook);

/*
 * Register a plain function hook.
 */
void hook_register_function(hook_registry_t *reg,
                             const char *function_name,
                             int hook_type,
                             uint8_t span_kind,
                             hook_pre_fn pre_hook,
                             hook_post_fn post_hook);

/*
 * Register a plain function hook with a per-hook minimum duration threshold.
 * Calls faster than min_duration_ms are silently skipped (no span created).
 */
void hook_register_function_threshold(hook_registry_t *reg,
                                       const char *function_name,
                                       int hook_type,
                                       uint8_t span_kind,
                                       double min_duration_ms,
                                       hook_pre_fn pre_hook,
                                       hook_post_fn post_hook);

/*
 * Register a side-effect interceptor (no span, just intercept).
 */
void hook_register_side_effect(hook_registry_t *reg,
                                const char *function_name,
                                hook_side_fn callback);

/*
 * Look up a hook for the given execute_data.
 * Returns the hook_entry_t* or NULL if no match.
 * hook_type_filter: HOOK_TYPE_INTERNAL or HOOK_TYPE_USERLAND
 */
hook_entry_t *hook_registry_find(hook_registry_t *reg,
                                  hook_ce_cache_t *cache,
                                  zend_execute_data *execute_data,
                                  int hook_type_filter);

/*
 * Run all matching side-effect hooks.
 */
void hook_registry_run_side_effects(hook_registry_t *reg,
                                     profiler_state_t *state,
                                     zend_execute_data *execute_data);

/* ── Global registry instance ── */

extern hook_registry_t g_hook_registry;

#endif /* HOOK_REGISTRY_H */
