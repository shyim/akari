#include "hook_registry.h"
#include "profiler_internal.h"

#include <string.h>
#include <ctype.h>

/* ── Global registry ── */

hook_registry_t g_hook_registry;

/* ── Helpers ── */

static void str_to_lower(char *dst, const char *src, int max_len)
{
    int i = 0;
    for (; src[i] && i < max_len - 1; i++) {
        dst[i] = (char)tolower((unsigned char)src[i]);
    }
    dst[i] = '\0';
}

/* ── Build hash key for a hook entry ── */

static zend_string *build_hook_key(const char *class_name, int class_len,
                                     const char *method_name, int method_len,
                                     int hook_type)
{
    /* Key format: "T:class::method" or "T:function" where T is hook_type digit */
    char buf[512];
    int len;
    if (class_len > 0) {
        len = snprintf(buf, sizeof(buf), "%d:%.*s::%.*s",
                       hook_type, class_len, class_name, method_len, method_name);
    } else {
        len = snprintf(buf, sizeof(buf), "%d:%.*s",
                       hook_type, method_len, method_name);
    }
    return zend_string_init(buf, len, 1); /* persistent */
}

/* ── Registry API ── */

void hook_registry_init(hook_registry_t *reg)
{
    memset(reg, 0, sizeof(hook_registry_t));
    reg->hook_map = pemalloc(sizeof(HashTable), 1);
    zend_hash_init(reg->hook_map, 128, NULL, NULL, 1); /* persistent */
    reg->side_map = pemalloc(sizeof(HashTable), 1);
    zend_hash_init(reg->side_map, 8, NULL, NULL, 1);
}

void hook_registry_reset(hook_registry_t *reg)
{
    /* Clear resolved class entries (they become invalid between requests) */
    for (int i = 0; i < reg->count; i++) {
        reg->entries[i].resolved_ce = NULL;
        reg->entries[i].resolve_attempted = 0;
    }
    for (int i = 0; i < reg->wildcard_count; i++) {
        reg->wildcards[i].resolved_ce = NULL;
        reg->wildcards[i].resolve_attempted = 0;
    }
}

void hook_registry_destroy(hook_registry_t *reg)
{
    if (reg->hook_map) {
        zend_hash_destroy(reg->hook_map);
        pefree(reg->hook_map, 1);
        reg->hook_map = NULL;
    }
    if (reg->side_map) {
        zend_hash_destroy(reg->side_map);
        pefree(reg->side_map, 1);
        reg->side_map = NULL;
    }
}

static void add_to_hash(hook_registry_t *reg, hook_entry_t *e)
{
    zend_string *key = build_hook_key(e->class_name, e->class_name_len,
                                       e->method_name, e->method_name_len,
                                       e->hook_type);
    zend_hash_add_ptr(reg->hook_map, key, e);
    zend_string_release(key);
}

void hook_register_method(hook_registry_t *reg,
                           const char *class_name,
                           const char *method_name,
                           int hook_type,
                           uint8_t span_kind,
                           int use_instanceof,
                           hook_pre_fn pre_hook,
                           hook_post_fn post_hook)
{
    if (reg->count >= HOOK_REGISTRY_MAX) return;

    hook_entry_t *e = &reg->entries[reg->count];
    memset(e, 0, sizeof(hook_entry_t));

    str_to_lower(e->class_name, class_name, sizeof(e->class_name));
    e->class_name_len = (int)strlen(e->class_name);
    str_to_lower(e->method_name, method_name, sizeof(e->method_name));
    e->method_name_len = (int)strlen(e->method_name);

    e->hook_type = hook_type;
    e->span_kind = span_kind;
    e->use_instanceof = use_instanceof;
    e->method_filter = NULL;
    e->pre_hook = pre_hook;
    e->post_hook = post_hook;

    reg->count++;
    add_to_hash(reg, e);
}

void hook_register_class_wildcard(hook_registry_t *reg,
                                   const char *class_name,
                                   int hook_type,
                                   uint8_t span_kind,
                                   int use_instanceof,
                                   hook_method_filter_fn filter,
                                   hook_pre_fn pre_hook,
                                   hook_post_fn post_hook)
{
    if (reg->wildcard_count >= HOOK_WILDCARD_MAX) return;

    hook_entry_t *e = &reg->wildcards[reg->wildcard_count];
    memset(e, 0, sizeof(hook_entry_t));

    str_to_lower(e->class_name, class_name, sizeof(e->class_name));
    e->class_name_len = (int)strlen(e->class_name);
    e->method_name[0] = '*';
    e->method_name[1] = '\0';
    e->method_name_len = 1;

    e->hook_type = hook_type;
    e->span_kind = span_kind;
    e->use_instanceof = use_instanceof;
    e->method_filter = filter;
    e->pre_hook = pre_hook;
    e->post_hook = post_hook;

    reg->wildcard_count++;
    /* Wildcards are NOT added to the hash — they use linear scan (small array) */
}

void hook_register_function(hook_registry_t *reg,
                             const char *function_name,
                             int hook_type,
                             uint8_t span_kind,
                             hook_pre_fn pre_hook,
                             hook_post_fn post_hook)
{
    if (reg->count >= HOOK_REGISTRY_MAX) return;

    hook_entry_t *e = &reg->entries[reg->count];
    memset(e, 0, sizeof(hook_entry_t));

    e->class_name[0] = '\0';
    e->class_name_len = 0;

    str_to_lower(e->method_name, function_name, sizeof(e->method_name));
    e->method_name_len = (int)strlen(e->method_name);

    e->hook_type = hook_type;
    e->span_kind = span_kind;
    e->use_instanceof = 0;
    e->method_filter = NULL;
    e->pre_hook = pre_hook;
    e->post_hook = post_hook;

    reg->count++;
    add_to_hash(reg, e);
}

void hook_register_function_threshold(hook_registry_t *reg,
                                       const char *function_name,
                                       int hook_type,
                                       uint8_t span_kind,
                                       double min_duration_ms,
                                       hook_pre_fn pre_hook,
                                       hook_post_fn post_hook)
{
    if (reg->count >= HOOK_REGISTRY_MAX) return;

    hook_entry_t *e = &reg->entries[reg->count];
    memset(e, 0, sizeof(hook_entry_t));

    e->class_name[0] = '\0';
    e->class_name_len = 0;

    str_to_lower(e->method_name, function_name, sizeof(e->method_name));
    e->method_name_len = (int)strlen(e->method_name);

    e->hook_type = hook_type;
    e->span_kind = span_kind;
    e->use_instanceof = 0;
    e->method_filter = NULL;
    e->min_duration_ns = (uint64_t)(min_duration_ms * 1000000.0);
    e->pre_hook = pre_hook;
    e->post_hook = post_hook;

    reg->count++;
    add_to_hash(reg, e);
}

void hook_register_side_effect(hook_registry_t *reg,
                                const char *function_name,
                                hook_side_fn callback)
{
    if (reg->side_count >= HOOK_SIDE_MAX) return;

    hook_side_entry_t *e = &reg->side_entries[reg->side_count];
    memset(e, 0, sizeof(hook_side_entry_t));

    e->class_name[0] = '\0';
    e->class_name_len = 0;
    str_to_lower(e->method_name, function_name, sizeof(e->method_name));
    e->method_name_len = (int)strlen(e->method_name);
    e->callback = callback;

    /* Add to side-effect hash */
    char buf[256];
    int len = snprintf(buf, sizeof(buf), "%s", e->method_name);
    zend_string *key = zend_string_init(buf, len, 1);
    zend_hash_add_ptr(reg->side_map, key, e);
    zend_string_release(key);

    reg->side_count++;
}

/* ── Resolve class entry lazily ── */

static void resolve_class_entry(hook_entry_t *e)
{
    if (e->resolve_attempted) return;
    e->resolve_attempted = 1;

    if (e->class_name_len == 0) return;

    zend_string *name = zend_string_init(e->class_name, e->class_name_len, 0);
    e->resolved_ce = zend_lookup_class(name);
    zend_string_release(name);
}

/* ── Class match helper ── */

static int class_matches(hook_entry_t *e, zend_class_entry *ce)
{
    resolve_class_entry(e);
    if (!e->resolved_ce) return 0;

    if (e->use_instanceof) {
        return (ce == e->resolved_ce || instanceof_function(ce, e->resolved_ce));
    }
    return (ce == e->resolved_ce);
}

/* ── O(1) Lookup ── */

/* Build lookup key in stack buffer without allocation.
 * Returns key length, or 0 on overflow. */
static int build_lookup_key(char *buf, size_t buf_size,
                             int hook_type,
                             const char *class_name, size_t class_len,
                             const char *method_name, size_t method_len)
{
    /* Key format: "T:class::method" or "T:function" */
    char *p = buf;
    char *end = buf + buf_size - 1;

    *p++ = '0' + hook_type;
    *p++ = ':';

    if (class_len > 0) {
        for (size_t i = 0; i < class_len && p < end; i++) {
            *p++ = (char)tolower((unsigned char)class_name[i]);
        }
        if (p + 2 >= end) return 0;
        *p++ = ':';
        *p++ = ':';
    }

    for (size_t i = 0; i < method_len && p < end; i++) {
        *p++ = (char)tolower((unsigned char)method_name[i]);
    }

    *p = '\0';
    return (int)(p - buf);
}

hook_entry_t *hook_registry_find(hook_registry_t *reg,
                                  zend_execute_data *execute_data,
                                  int hook_type_filter)
{
    if (!execute_data || !execute_data->func) return NULL;

    zend_function *func = execute_data->func;
    if (!func->common.function_name) return NULL;

    const char *fname = ZSTR_VAL(func->common.function_name);
    size_t fname_len = ZSTR_LEN(func->common.function_name);
    int has_scope = (func->common.scope != NULL);

    char key_buf[512];
    int key_len;

    if (has_scope) {
        const char *cname = ZSTR_VAL(func->common.scope->name);
        size_t cname_len = ZSTR_LEN(func->common.scope->name);
        key_len = build_lookup_key(key_buf, sizeof(key_buf), hook_type_filter,
                                    cname, cname_len, fname, fname_len);
    } else {
        key_len = build_lookup_key(key_buf, sizeof(key_buf), hook_type_filter,
                                    NULL, 0, fname, fname_len);
    }

    if (key_len == 0) return NULL;

    /* O(1) hash lookup — covers exact class::method and plain function matches */
    hook_entry_t *result = zend_hash_str_find_ptr(reg->hook_map, key_buf, key_len);

    if (result) {
        /* For instanceof hooks, verify the actual class matches */
        if (result->use_instanceof && result->class_name_len > 0 && has_scope) {
            if (!class_matches(result, func->common.scope)) {
                result = NULL;
            }
        }
        if (result) return result;
    }

    /* For class methods: try parent classes and interfaces for instanceof hooks */
    if (has_scope) {
        zend_class_entry *ce = func->common.scope;

        /* Walk parent chain */
        zend_class_entry *parent = ce->parent;
        while (parent) {
            key_len = build_lookup_key(key_buf, sizeof(key_buf), hook_type_filter,
                                        ZSTR_VAL(parent->name), ZSTR_LEN(parent->name),
                                        fname, fname_len);
            if (key_len > 0) {
                result = zend_hash_str_find_ptr(reg->hook_map, key_buf, key_len);
                if (result && result->use_instanceof) {
                    return result;
                }
            }
            parent = parent->parent;
        }

        /* Check interfaces */
        if (ce->num_interfaces > 0) {
            for (uint32_t i = 0; i < ce->num_interfaces; i++) {
                zend_class_entry *iface = ce->interfaces[i];
                key_len = build_lookup_key(key_buf, sizeof(key_buf), hook_type_filter,
                                            ZSTR_VAL(iface->name), ZSTR_LEN(iface->name),
                                            fname, fname_len);
                if (key_len > 0) {
                    result = zend_hash_str_find_ptr(reg->hook_map, key_buf, key_len);
                    if (result && result->use_instanceof) {
                        return result;
                    }
                }
            }
        }

        /* Wildcard class hooks (small array, typically 4-6 entries) */
        for (int i = 0; i < reg->wildcard_count; i++) {
            hook_entry_t *e = &reg->wildcards[i];
            if (e->hook_type != hook_type_filter) continue;
            if (!class_matches(e, func->common.scope)) continue;
            if (e->method_filter && !e->method_filter(fname)) continue;
            return e;
        }
    }

    return NULL;
}

void hook_registry_run_side_effects(hook_registry_t *reg,
                                     profiler_state_t *state,
                                     zend_execute_data *execute_data)
{
    if (reg->side_count == 0) return;
    if (!execute_data || !execute_data->func) return;

    zend_function *func = execute_data->func;
    if (!func->common.function_name || func->common.scope) return;

    /* Build lowercase key in stack buffer — no allocation */
    const char *fname = ZSTR_VAL(func->common.function_name);
    size_t fname_len = ZSTR_LEN(func->common.function_name);
    char fname_lower[128];
    if (fname_len >= sizeof(fname_lower)) return;
    for (size_t i = 0; i < fname_len; i++) {
        fname_lower[i] = (char)tolower((unsigned char)fname[i]);
    }

    hook_side_entry_t *e = zend_hash_str_find_ptr(reg->side_map, fname_lower, fname_len);
    if (e) {
        e->callback(state, execute_data);
    }
}
