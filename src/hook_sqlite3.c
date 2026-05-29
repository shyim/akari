#include "profiler_internal.h"
#include "hook_registry.h"

/*
 * SQLite3 instrumentation — hooks the ext-sqlite3 procedural class API
 * (SQLite3, SQLite3Stmt), as opposed to PDO with the pdo_sqlite driver
 * which is handled in hook_pdo.c.
 *
 * Records db.system=sqlite and the SQL as db.statement.
 *
 * Prepared statements: ext-sqlite3 does not install public headers, so we
 * cannot read the inner sqlite3_stmt* from the SQLite3Stmt object without
 * depending on PHP's private struct layout (no ABI guarantee). Instead we
 * capture the SQL when SQLite3::prepare() returns and remember it keyed by
 * the SQLite3Stmt object handle, then attach it on SQLite3Stmt::execute().
 * This mirrors the request-scoped object map used in hook_curl.c.
 */

/* ── Request-scoped map: SQLite3Stmt object handle → prepared SQL ──
 * Stored in module globals so it is per-thread under ZTS. */
#define sqlite3_stmt_sql  AKARI_G(sqlite3_stmt_sql)

static void sqlite3_map_init(void)
{
    if (!sqlite3_stmt_sql) {
        ALLOC_HASHTABLE(sqlite3_stmt_sql);
        zend_hash_init(sqlite3_stmt_sql, 8, NULL, ZVAL_PTR_DTOR, 0);
    }
}

void sqlite3_rshutdown(void)
{
    if (sqlite3_stmt_sql) {
        zend_hash_destroy(sqlite3_stmt_sql);
        FREE_HASHTABLE(sqlite3_stmt_sql);
        sqlite3_stmt_sql = NULL;
    }
}

static void sqlite3_store_stmt_sql(zval *stmt_obj, zval *sql)
{
    if (!stmt_obj || Z_TYPE_P(stmt_obj) != IS_OBJECT) return;
    if (!sql || Z_TYPE_P(sql) != IS_STRING) return;
    sqlite3_map_init();

    uintptr_t key = (uintptr_t)Z_OBJ_P(stmt_obj);
    zval copy;
    ZVAL_COPY(&copy, sql);
    zend_hash_index_update(sqlite3_stmt_sql, key, &copy);
}

static zval *sqlite3_lookup_stmt_sql(zval *stmt_obj)
{
    if (!sqlite3_stmt_sql || !stmt_obj || Z_TYPE_P(stmt_obj) != IS_OBJECT) return NULL;
    uintptr_t key = (uintptr_t)Z_OBJ_P(stmt_obj);
    return zend_hash_index_find(sqlite3_stmt_sql, key);
}

/* ── Shared attribute recorder ── */

static profiler_db_attr_t *sqlite3_alloc_attr(profiler_state_t *state, uint32_t span_index)
{
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
    strcpy(attr->db_system, "sqlite");
    state->db_attr_count++;
    return attr;
}

static void sqlite3_set_statement(profiler_db_attr_t *attr, const char *sql, size_t len)
{
    if (len >= DB_STATEMENT_MAX) len = DB_STATEMENT_MAX - 1;
    memcpy(attr->db_statement, sql, len);
    attr->db_statement[len] = '\0';
    attr->db_statement_len = len;
}

/* ── Callbacks ── */

/* query / querySingle / exec / prepare: SQL is the first argument. */
static void *sqlite3_query_pre(profiler_state_t *state, zend_execute_data *execute_data,
                                profiler_span_t *span, uint32_t span_index)
{
    (void)span;
    profiler_db_attr_t *attr = sqlite3_alloc_attr(state, span_index);
    if (!attr) return NULL;

    if (ZEND_CALL_NUM_ARGS(execute_data) >= 1) {
        zval *sql_arg = ZEND_CALL_ARG(execute_data, 1);
        if (sql_arg && Z_TYPE_P(sql_arg) == IS_STRING) {
            sqlite3_set_statement(attr, Z_STRVAL_P(sql_arg), Z_STRLEN_P(sql_arg));
        }
    }
    return NULL;
}

/* prepare post-hook: remember the SQL keyed by the returned SQLite3Stmt so
 * execute() can recover it. */
static void sqlite3_prepare_post(profiler_state_t *state, zend_execute_data *execute_data,
                                  zval *return_value, profiler_span_t *span,
                                  uint32_t span_index, void *pre_data)
{
    (void)state; (void)span; (void)span_index; (void)pre_data;
    if (!return_value || Z_TYPE_P(return_value) != IS_OBJECT) return;
    if (ZEND_CALL_NUM_ARGS(execute_data) < 1) return;

    zval *sql_arg = ZEND_CALL_ARG(execute_data, 1);
    sqlite3_store_stmt_sql(return_value, sql_arg);
}

/* SQLite3Stmt::execute: recover the SQL captured at prepare() time. */
static void *sqlite3_stmt_execute_pre(profiler_state_t *state, zend_execute_data *execute_data,
                                       profiler_span_t *span, uint32_t span_index)
{
    (void)span;
    profiler_db_attr_t *attr = sqlite3_alloc_attr(state, span_index);
    if (!attr) return NULL;

    zval *this_obj = &execute_data->This;
    zval *sql = sqlite3_lookup_stmt_sql(this_obj);
    if (sql && Z_TYPE_P(sql) == IS_STRING) {
        sqlite3_set_statement(attr, Z_STRVAL_P(sql), Z_STRLEN_P(sql));
    }
    return NULL;
}

/* ── Registration ── */

void hook_sqlite3_register(hook_registry_t *reg)
{
    hook_register_method(reg, "SQLite3", "query",
        HOOK_TYPE_INTERNAL, SPAN_KIND_CLIENT, 1, sqlite3_query_pre, NULL);
    hook_register_method(reg, "SQLite3", "querySingle",
        HOOK_TYPE_INTERNAL, SPAN_KIND_CLIENT, 1, sqlite3_query_pre, NULL);
    hook_register_method(reg, "SQLite3", "exec",
        HOOK_TYPE_INTERNAL, SPAN_KIND_CLIENT, 1, sqlite3_query_pre, NULL);
    hook_register_method(reg, "SQLite3", "prepare",
        HOOK_TYPE_INTERNAL, SPAN_KIND_CLIENT, 1, sqlite3_query_pre, sqlite3_prepare_post);
    hook_register_method(reg, "SQLite3Stmt", "execute",
        HOOK_TYPE_INTERNAL, SPAN_KIND_CLIENT, 1, sqlite3_stmt_execute_pre, NULL);
}
