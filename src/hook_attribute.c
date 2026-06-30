#include "profiler_internal.h"
#include "Zend/zend_attributes.h"
#include "Zend/zend_API.h"
#include "../akari_arginfo.h"

/*
 * hook_attribute.c — userland #[Akari\Span] attribute support.
 *
 * Lets users mark any method/function with #[Akari\Span] to create a span for
 * every call while profiling is active, without registering a built-in hook.
 *
 *   use Akari\Span;
 *   class Foo {
 *       #[Span]                          -> span "Foo::bar"
 *       public function bar() {}
 *       #[Span(name: "work")]            -> span "work"
 *       public function baz() {}
 *       #[Span(minDurationMs: 5.0)]      -> span dropped if faster than 5ms
 *       public function slow() {}
 *   }
 *
 * Performance design
 * ------------------
 * The attribute HashTable on an op_array is read AT MOST ONCE per function per
 * request. The verdict (has-span + name + threshold) is memoized in a per-request
 * HashTable keyed by the op_array pointer. Every later call is a single
 * pointer-keyed hash lookup — no attribute walking on the hot path. Same
 * "decide once, branch forever" model the built-in registry uses, resolved
 * lazily at first call because userland classes do not exist at MINIT.
 */

/* The registered attribute class entry (per process). */
static zend_class_entry *akari_span_attr_ce = NULL;

/* Cached verdict for one op_array. */
typedef struct {
    uint8_t has_span;                 /* 1 if #[Akari\Span] present */
    uint32_t name_len;                /* length of custom name, 0 = derive from func */
    uint64_t min_duration_ns;         /* per-span drop threshold, 0 = none */
    char name[SPAN_NAME_OVERRIDE_MAX];
} attr_verdict_t;

/* ── Attribute class: #[Akari\Span(?string $name = null, float $minDurationMs = 0.0)] ── */

ZEND_METHOD(Akari_Span, __construct)
{
    zend_string *name = NULL;
    double min_duration_ms = 0.0;

    ZEND_PARSE_PARAMETERS_START(0, 2)
        Z_PARAM_OPTIONAL
        Z_PARAM_STR_OR_NULL(name)
        Z_PARAM_DOUBLE(min_duration_ms)
    ZEND_PARSE_PARAMETERS_END();

    zval zv;
    if (name) {
        ZVAL_STR_COPY(&zv, name);
    } else {
        ZVAL_NULL(&zv);
    }
    zend_update_property(akari_span_attr_ce, Z_OBJ_P(ZEND_THIS), "name", sizeof("name") - 1, &zv);
    zval_ptr_dtor(&zv);

    ZVAL_DOUBLE(&zv, min_duration_ms);
    zend_update_property(akari_span_attr_ce, Z_OBJ_P(ZEND_THIS),
                         "minDurationMs", sizeof("minDurationMs") - 1, &zv);
}

void hook_attribute_register_class(void)
{
    if (akari_span_attr_ce) return;

    zend_class_entry ce;
    INIT_NS_CLASS_ENTRY(ce, "Akari", "Span", class_Akari_Span_methods);
    akari_span_attr_ce = zend_register_internal_class(&ce);
    akari_span_attr_ce->ce_flags |= ZEND_ACC_FINAL;

    /* Declare the constructor-promoted properties so reflection sees them.
     * zend_declare_typed_property takes ownership of the (permanent, interned)
     * name string — do not release it here. */
    zval default_name;
    ZVAL_NULL(&default_name);
    zend_declare_typed_property(akari_span_attr_ce,
        zend_string_init_interned("name", sizeof("name") - 1, 1), &default_name,
        ZEND_ACC_PUBLIC, NULL,
        (zend_type) ZEND_TYPE_INIT_MASK(MAY_BE_STRING | MAY_BE_NULL));

    zval default_min;
    ZVAL_DOUBLE(&default_min, 0.0);
    zend_declare_typed_property(akari_span_attr_ce,
        zend_string_init_interned("minDurationMs", sizeof("minDurationMs") - 1, 1), &default_min,
        ZEND_ACC_PUBLIC, NULL,
        (zend_type) ZEND_TYPE_INIT_MASK(MAY_BE_DOUBLE));

    /* Mark it usable as #[Attribute] on methods and functions. */
    zend_internal_attribute_register(akari_span_attr_ce,
        ZEND_ATTRIBUTE_TARGET_METHOD | ZEND_ATTRIBUTE_TARGET_FUNCTION);
}

/* Read the i-th attribute arg as a string into out (NUL-terminated, capped). */
static uint32_t read_string_arg(zend_attribute *attr, uint32_t i, zend_class_entry *scope,
                                char *out, size_t out_size)
{
    zval tmp;
    uint32_t len = 0;
    if (zend_get_attribute_value(&tmp, attr, i, scope) == SUCCESS) {
        if (Z_TYPE(tmp) == IS_STRING) {
            size_t l = Z_STRLEN(tmp);
            if (l >= out_size) l = out_size - 1;
            memcpy(out, Z_STRVAL(tmp), l);
            out[l] = '\0';
            len = (uint32_t)l;
        }
        zval_ptr_dtor(&tmp);
    }
    return len;
}

/* Read the i-th attribute arg as a double (returns 0.0 on absence/type mismatch). */
static double read_double_arg(zend_attribute *attr, uint32_t i, zend_class_entry *scope)
{
    zval tmp;
    double d = 0.0;
    if (zend_get_attribute_value(&tmp, attr, i, scope) == SUCCESS) {
        if (Z_TYPE(tmp) == IS_DOUBLE) {
            d = Z_DVAL(tmp);
        } else if (Z_TYPE(tmp) == IS_LONG) {
            d = (double)Z_LVAL(tmp);
        }
        zval_ptr_dtor(&tmp);
    }
    return d;
}

/* Scan an op_array's attributes for #[Akari\Span], filling the verdict.
 *
 * Args may be positional (#[Span("n", 5.0)]) or named (#[Span(minDurationMs: 5)]).
 * zend_attribute stores positional args first (indices 0..), then named args.
 * We match named args by name and otherwise fall back to positional order. */
static void scan_attributes(zend_op_array *op_array, attr_verdict_t *out)
{
    out->has_span = 0;
    out->name_len = 0;
    out->min_duration_ns = 0;
    out->name[0] = '\0';

    if (!op_array->attributes) return;

    zend_attribute *attr = zend_get_attribute_str(op_array->attributes,
                                                  "akari\\span", sizeof("akari\\span") - 1);
    if (!attr) return;

    out->has_span = 1;

    double min_ms = 0.0;
    zend_class_entry *scope = op_array->scope;

    for (uint32_t i = 0; i < attr->argc; i++) {
        zend_string *arg_name = attr->args[i].name;   /* NULL for positional */
        if (arg_name) {
            if (zend_string_equals_literal(arg_name, "name")) {
                out->name_len = read_string_arg(attr, i, scope, out->name, sizeof(out->name));
            } else if (zend_string_equals_literal(arg_name, "minDurationMs")) {
                min_ms = read_double_arg(attr, i, scope);
            }
        } else {
            /* Positional: 0 = name, 1 = minDurationMs */
            if (i == 0) {
                out->name_len = read_string_arg(attr, i, scope, out->name, sizeof(out->name));
            } else if (i == 1) {
                min_ms = read_double_arg(attr, i, scope);
            }
        }
    }

    if (min_ms < 0.0) min_ms = 0.0;
    if (min_ms > 10000.0) min_ms = 10000.0;   /* clamp to 10s, matching event-dispatch cap */
    out->min_duration_ns = (uint64_t)(min_ms * 1000000.0);
}

/* HashTable value destructor: frees a persistently-allocated verdict. */
static void verdict_dtor(zval *zv)
{
    pefree(Z_PTR_P(zv), 1);
}

/*
 * Resolve whether this execute_data's function carries #[Akari\Span].
 * Returns 1 and fills the out params if a span should be created.
 * Uses the per-request op_array-keyed cache to scan only once per function.
 */
int hook_attribute_lookup(profiler_state_t *state, zend_execute_data *execute_data,
                          const char **out_name, uint32_t *out_name_len,
                          uint64_t *out_min_duration_ns)
{
    if (!akari_span_attr_ce) return 0;

    zend_function *func = execute_data->func;
    if (!func || func->type != ZEND_USER_FUNCTION) return 0;

    zend_op_array *op_array = &func->op_array;

    HashTable *cache = state->attr_cache;
    if (!cache) {
        cache = pemalloc(sizeof(HashTable), 1);
        if (!cache) return 0;
        /* Persistent (malloc-backed) so the cache's lifetime is independent of
         * the request's emalloc heap — it is created lazily on first call and
         * freed explicitly at request/thread shutdown, after the emalloc heap
         * for the request may already be gone. The destructor frees each
         * persistently-allocated verdict. */
        zend_hash_init(cache, 64, NULL, verdict_dtor, 1);
        state->attr_cache = cache;
    }

    zend_ulong key = (zend_ulong)(uintptr_t)op_array;
    attr_verdict_t *v = zend_hash_index_find_ptr(cache, key);
    if (!v) {
        v = pemalloc(sizeof(attr_verdict_t), 1);
        if (!v) return 0;
        scan_attributes(op_array, v);
        zend_hash_index_add_ptr(cache, key, v);
    }

    if (!v->has_span) return 0;

    if (v->name_len > 0) {
        *out_name = v->name;
        *out_name_len = v->name_len;
    } else {
        *out_name = NULL;       /* caller derives "Class::method" */
        *out_name_len = 0;
    }
    *out_min_duration_ns = v->min_duration_ns;
    return 1;
}

/* Free the per-request attribute cache (called at request shutdown). The
 * hashtable's destructor (verdict_dtor) frees each stored verdict. */
void hook_attribute_cache_free(profiler_state_t *state)
{
    if (!state->attr_cache) return;
    HashTable *cache = state->attr_cache;
    zend_hash_destroy(cache);
    pefree(cache, 1);
    state->attr_cache = NULL;
}
