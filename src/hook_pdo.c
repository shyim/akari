#include "profiler_internal.h"
#include "hook_registry.h"
#include "ext/pdo/php_pdo_driver.h"

/* ── PDO detection ──
 * The resolved PDO / PDOStatement class entries live in module globals so they
 * are per-thread under ZTS (a zend_class_entry* resolved on one thread is not
 * valid on another). Aliased to the original names to keep the body unchanged. */
#define otel_pdo_dbh_ce   AKARI_G(pdo_dbh_ce)
#define otel_pdo_stmt_ce  AKARI_G(pdo_stmt_ce)

void profiler_resolve_pdo_classes(void)
{
    if (otel_pdo_dbh_ce) return;   /* per-thread: resolved once for this thread */
    zend_string *name;

    name = zend_string_init("PDO", 3, 0);
    otel_pdo_dbh_ce = zend_lookup_class(name);
    zend_string_release(name);

    name = zend_string_init("PDOStatement", 12, 0);
    otel_pdo_stmt_ce = zend_lookup_class(name);
    zend_string_release(name);
}

/* ── Attribute recording (shared by all PDO hooks) ── */

static void record_pdo_attrs_internal(profiler_state_t *state, zend_execute_data *execute_data,
                                       uint32_t span_index, const char *method)
{
    /* Ensure capacity */
    if (state->db_attr_count >= state->db_attr_capacity) {
        size_t new_cap = state->db_attr_capacity ? state->db_attr_capacity * 2 : PROFILER_INITIAL_DB_ATTRS;
        profiler_db_attr_t *new_attrs = realloc(state->db_attrs, new_cap * sizeof(profiler_db_attr_t));
        if (!new_attrs) return;
        state->db_attrs = new_attrs;
        state->db_attr_capacity = new_cap;
    }

    profiler_db_attr_t *attr = &state->db_attrs[state->db_attr_count];
    memset(attr, 0, sizeof(profiler_db_attr_t));
    attr->span_index = span_index;

    zval *this_obj = &execute_data->This;
    if (Z_TYPE_P(this_obj) != IS_OBJECT) return;

    zend_class_entry *ce = Z_OBJCE_P(this_obj);

    /* Extract SQL query from parameters */
    if (strcmp(method, "query") == 0 || strcmp(method, "exec") == 0) {
        /* First arg is the SQL string */
        uint32_t num_args = ZEND_CALL_NUM_ARGS(execute_data);
        if (num_args >= 1) {
            zval *query_arg = ZEND_CALL_ARG(execute_data, 1);
            if (query_arg && Z_TYPE_P(query_arg) == IS_STRING) {
                size_t copy_len = Z_STRLEN_P(query_arg);
                if (copy_len >= DB_STATEMENT_MAX) copy_len = DB_STATEMENT_MAX - 1;
                memcpy(attr->db_statement, Z_STRVAL_P(query_arg), copy_len);
                attr->db_statement[copy_len] = '\0';
                attr->db_statement_len = copy_len;
            }
        }
    }

    /* Extract db_system, db_name, db_user from PDO handle */
    pdo_dbh_t *dbh = NULL;

    if (otel_pdo_dbh_ce && (ce == otel_pdo_dbh_ce || instanceof_function(ce, otel_pdo_dbh_ce))) {
        pdo_dbh_object_t *dbh_obj = php_pdo_dbh_fetch_object(Z_OBJ_P(this_obj));
        if (dbh_obj) dbh = dbh_obj->inner;
    } else if (otel_pdo_stmt_ce && (ce == otel_pdo_stmt_ce || instanceof_function(ce, otel_pdo_stmt_ce))) {
        /* PDOStatement — get dbh from statement */
        pdo_stmt_t *stmt = php_pdo_stmt_fetch_object(Z_OBJ_P(this_obj));
        if (stmt) {
            dbh = stmt->dbh;
            /* For prepared statements, get query from stmt->query_string */
            if (attr->db_statement_len == 0 && stmt->query_string) {
                size_t copy_len = ZSTR_LEN(stmt->query_string);
                if (copy_len >= DB_STATEMENT_MAX) copy_len = DB_STATEMENT_MAX - 1;
                memcpy(attr->db_statement, ZSTR_VAL(stmt->query_string), copy_len);
                attr->db_statement[copy_len] = '\0';
                attr->db_statement_len = copy_len;
            }
        }
    }

    if (dbh) {
        /* db.system from driver name */
        if (dbh->driver && dbh->driver->driver_name) {
            snprintf(attr->db_system, DB_SYSTEM_MAX, "%s", dbh->driver->driver_name);
        }

        /* db.user */
        if (dbh->username) {
            snprintf(attr->db_user, DB_USER_MAX, "%s", dbh->username);
        }

        /* db.name — parse from data_source (e.g. "mysql:host=localhost;dbname=test") */
        if (dbh->data_source) {
            const char *dbname = strstr(dbh->data_source, "dbname=");
            if (dbname) {
                dbname += 7;
                const char *end = strchr(dbname, ';');
                size_t len = end ? (size_t)(end - dbname) : strlen(dbname);
                if (len >= DB_NAME_MAX) len = DB_NAME_MAX - 1;
                memcpy(attr->db_name, dbname, len);
                attr->db_name[len] = '\0';
            }
        }
    }

    state->db_attr_count++;
}

/* ── Registry callbacks ── */

static void *pdo_query_pre(profiler_state_t *state, zend_execute_data *execute_data,
                            profiler_span_t *span, uint32_t span_index)
{
    (void)span;
    record_pdo_attrs_internal(state, execute_data, span_index, "query");
    return NULL;
}

static void *pdo_exec_pre(profiler_state_t *state, zend_execute_data *execute_data,
                           profiler_span_t *span, uint32_t span_index)
{
    (void)span;
    record_pdo_attrs_internal(state, execute_data, span_index, "exec");
    return NULL;
}

static void *pdo_stmt_execute_pre(profiler_state_t *state, zend_execute_data *execute_data,
                                   profiler_span_t *span, uint32_t span_index)
{
    (void)span;
    record_pdo_attrs_internal(state, execute_data, span_index, "execute");
    return NULL;
}

/* Transaction methods: no SQL argument, just record db metadata */
static void *pdo_txn_pre(profiler_state_t *state, zend_execute_data *execute_data,
                          profiler_span_t *span, uint32_t span_index)
{
    (void)span;
    record_pdo_attrs_internal(state, execute_data, span_index, "beginTransaction");
    return NULL;
}

/* ── Registration ── */

void hook_pdo_register(hook_registry_t *reg)
{
    hook_register_method(reg, "PDO", "query",
        HOOK_TYPE_INTERNAL, SPAN_KIND_CLIENT, 1, pdo_query_pre, NULL);
    hook_register_method(reg, "PDO", "exec",
        HOOK_TYPE_INTERNAL, SPAN_KIND_CLIENT, 1, pdo_exec_pre, NULL);
    hook_register_method(reg, "PDOStatement", "execute",
        HOOK_TYPE_INTERNAL, SPAN_KIND_CLIENT, 1, pdo_stmt_execute_pre, NULL);
    hook_register_method(reg, "PDO", "beginTransaction",
        HOOK_TYPE_INTERNAL, SPAN_KIND_CLIENT, 1, pdo_txn_pre, NULL);
    hook_register_method(reg, "PDO", "commit",
        HOOK_TYPE_INTERNAL, SPAN_KIND_CLIENT, 1, pdo_txn_pre, NULL);
    hook_register_method(reg, "PDO", "rollBack",
        HOOK_TYPE_INTERNAL, SPAN_KIND_CLIENT, 1, pdo_txn_pre, NULL);
}
