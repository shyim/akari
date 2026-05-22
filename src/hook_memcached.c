#include "profiler_internal.h"
#include "hook_registry.h"

/*
 * Memcached instrumentation — hooks ext-memcached (Memcached class).
 *
 * Uses wildcard class registration with a method filter. Records
 * db.system=memcached and "COMMAND key" as db.statement.
 *
 * Also hooks ext-memcache (Memcache class) for legacy support.
 */

/* ── Command lists ── */

/* Commands that take a key as first argument */
static const char *mc_key_commands[] = {
    "get", "set", "add", "replace", "append", "prepend",
    "cas", "delete", "increment", "decrement", "touch",
    "getbykey", "setbykey", "addbykey", "replacebykey",
    "appendbykey", "prependbykey", "casbykey", "deletebykey",
    NULL
};

/* Commands with no key argument */
static const char *mc_nokey_commands[] = {
    "getmulti", "setmulti", "deletemulti",
    "getmultibykey", "setmultibykey", "deletemultibykey",
    "flush", "getallkeys", "getstats", "getversion",
    "fetch", "fetchall", "getdelayed", "getdelayedbykey",
    "quit",
    NULL
};

static int is_mc_key_command(const char *method)
{
    for (const char **cmd = mc_key_commands; *cmd; cmd++) {
        if (strcasecmp(method, *cmd) == 0) return 1;
    }
    return 0;
}

static int is_mc_command(const char *method)
{
    if (is_mc_key_command(method)) return 1;
    for (const char **cmd = mc_nokey_commands; *cmd; cmd++) {
        if (strcasecmp(method, *cmd) == 0) return 1;
    }
    return 0;
}

/* ── ext-memcache (legacy) commands ── */

static const char *memcache_commands[] = {
    "get", "set", "add", "replace", "delete",
    "increment", "decrement", "flush",
    "connect", "pconnect", "close",
    NULL
};

static int is_memcache_command(const char *method)
{
    for (const char **cmd = memcache_commands; *cmd; cmd++) {
        if (strcasecmp(method, *cmd) == 0) return 1;
    }
    return 0;
}

/* ── Callbacks ── */

static void *memcached_pre(profiler_state_t *state, zend_execute_data *execute_data,
                            profiler_span_t *span, uint32_t span_index)
{
    (void)span;
    if (!execute_data || !execute_data->func || !execute_data->func->common.function_name)
        return NULL;

    const char *method = ZSTR_VAL(execute_data->func->common.function_name);

    if (state->db_attr_count >= state->db_attr_capacity) {
        size_t new_cap = state->db_attr_capacity ? state->db_attr_capacity * 2 : PROFILER_INITIAL_DB_ATTRS;
        profiler_db_attr_t *new_attrs = realloc(state->db_attrs, new_cap * sizeof(profiler_db_attr_t));
        if (!new_attrs) return NULL;
        state->db_attrs = new_attrs;
        state->db_attr_capacity = new_cap;
    }

    profiler_db_attr_t *attr = &state->db_attrs[state->db_attr_count];
    memset(attr, 0, sizeof(profiler_db_attr_t));
    attr->span_index = span_index;
    strcpy(attr->db_system, "memcached");

    int has_key = is_mc_key_command(method);
    uint32_t num_args = ZEND_CALL_NUM_ARGS(execute_data);

    if (has_key && num_args >= 1) {
        zval *key_arg = ZEND_CALL_ARG(execute_data, 1);
        if (key_arg && Z_TYPE_P(key_arg) == IS_STRING) {
            int n = snprintf(attr->db_statement, DB_STATEMENT_MAX,
                "%s %s", method, Z_STRVAL_P(key_arg));
            attr->db_statement_len = (n > 0 && (size_t)n < DB_STATEMENT_MAX) ? (size_t)n : DB_STATEMENT_MAX - 1;
        } else {
            int n = snprintf(attr->db_statement, DB_STATEMENT_MAX, "%s", method);
            attr->db_statement_len = (n > 0 && (size_t)n < DB_STATEMENT_MAX) ? (size_t)n : DB_STATEMENT_MAX - 1;
        }
    } else {
        int n = snprintf(attr->db_statement, DB_STATEMENT_MAX, "%s", method);
        attr->db_statement_len = (n > 0 && (size_t)n < DB_STATEMENT_MAX) ? (size_t)n : DB_STATEMENT_MAX - 1;
    }

    state->db_attr_count++;
    return NULL;
}

static void *memcache_pre(profiler_state_t *state, zend_execute_data *execute_data,
                           profiler_span_t *span, uint32_t span_index)
{
    (void)span;
    if (!execute_data || !execute_data->func || !execute_data->func->common.function_name)
        return NULL;

    const char *method = ZSTR_VAL(execute_data->func->common.function_name);

    if (state->db_attr_count >= state->db_attr_capacity) {
        size_t new_cap = state->db_attr_capacity ? state->db_attr_capacity * 2 : PROFILER_INITIAL_DB_ATTRS;
        profiler_db_attr_t *new_attrs = realloc(state->db_attrs, new_cap * sizeof(profiler_db_attr_t));
        if (!new_attrs) return NULL;
        state->db_attrs = new_attrs;
        state->db_attr_capacity = new_cap;
    }

    profiler_db_attr_t *attr = &state->db_attrs[state->db_attr_count];
    memset(attr, 0, sizeof(profiler_db_attr_t));
    attr->span_index = span_index;
    strcpy(attr->db_system, "memcache");

    /* Most memcache commands take key as first arg */
    uint32_t num_args = ZEND_CALL_NUM_ARGS(execute_data);
    if (num_args >= 1) {
        zval *key_arg = ZEND_CALL_ARG(execute_data, 1);
        if (key_arg && Z_TYPE_P(key_arg) == IS_STRING) {
            int n = snprintf(attr->db_statement, DB_STATEMENT_MAX,
                "%s %s", method, Z_STRVAL_P(key_arg));
            attr->db_statement_len = (n > 0 && (size_t)n < DB_STATEMENT_MAX) ? (size_t)n : DB_STATEMENT_MAX - 1;
        } else {
            int n = snprintf(attr->db_statement, DB_STATEMENT_MAX, "%s", method);
            attr->db_statement_len = (n > 0 && (size_t)n < DB_STATEMENT_MAX) ? (size_t)n : DB_STATEMENT_MAX - 1;
        }
    } else {
        int n = snprintf(attr->db_statement, DB_STATEMENT_MAX, "%s", method);
        attr->db_statement_len = (n > 0 && (size_t)n < DB_STATEMENT_MAX) ? (size_t)n : DB_STATEMENT_MAX - 1;
    }

    state->db_attr_count++;
    return NULL;
}

/* ── Registration ── */

void hook_memcached_register(hook_registry_t *reg)
{
    /* ext-memcached: Memcached class */
    hook_register_class_wildcard(reg, "Memcached",
        HOOK_TYPE_INTERNAL, SPAN_KIND_CLIENT, 0,
        is_mc_command, memcached_pre, NULL);

    /* ext-memcache: Memcache class (legacy) */
    hook_register_class_wildcard(reg, "Memcache",
        HOOK_TYPE_INTERNAL, SPAN_KIND_CLIENT, 0,
        is_memcache_command, memcache_pre, NULL);
}
