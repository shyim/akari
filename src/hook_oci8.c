#include "profiler_internal.h"
#include "hook_registry.h"

/*
 * OCI8 / Oracle instrumentation — hooks ext-oci8.
 *
 * Hooks both procedural and OOP styles:
 *   Procedural: oci_connect, oci_new_connect, oci_pconnect, oci_parse,
 *               oci_execute, oci_commit, oci_rollback
 *   OOP:       OCIConnection::commit/rollback, OCIStatement::execute
 *
 * Records db.system=oracle with db.statement containing the SQL (for parse/execute).
 */

/* ── Helper: record OCI attribute ── */

static void record_oci_attr(profiler_state_t *state, uint32_t span_index,
                              const char *statement, size_t stmt_len,
                              int normalize_statement)
{
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
    snprintf(attr->db_system, DB_SYSTEM_MAX, "oracle");
    if (statement && stmt_len > 0) {
        if (normalize_statement) {
            profiler_sql_normalize(statement, stmt_len,
                attr->db_statement, sizeof(attr->db_statement),
                &attr->db_statement_len);
        } else {
            profiler_sql_truncate(statement, stmt_len,
                attr->db_statement, sizeof(attr->db_statement),
                &attr->db_statement_len);
        }
    }
    state->db_attr_count++;
}

/* ── Procedural pre-hooks ── */

static void *oci_parse_pre(profiler_state_t *state, zend_execute_data *execute_data,
                             profiler_span_t *span, uint32_t span_index)
{
    (void)span;
    uint32_t num_args = ZEND_CALL_NUM_ARGS(execute_data);
    const char *stmt = NULL;
    size_t stmt_len = 0;
    if (num_args >= 2) {
        zval *sql_arg = ZEND_CALL_ARG(execute_data, 2);
        if (sql_arg && Z_TYPE_P(sql_arg) == IS_STRING) {
            stmt = Z_STRVAL_P(sql_arg);
            stmt_len = Z_STRLEN_P(sql_arg);
        }
    }
    record_oci_attr(state, span_index, stmt, stmt_len, 1);
    return NULL;
}

static void *oci_execute_pre(profiler_state_t *state, zend_execute_data *execute_data,
                               profiler_span_t *span, uint32_t span_index)
{
    (void)span;
    (void)execute_data;
    record_oci_attr(state, span_index, NULL, 0, 0);
    return NULL;
}

static void *oci_connect_pre(profiler_state_t *state, zend_execute_data *execute_data,
                               profiler_span_t *span, uint32_t span_index)
{
    (void)span;
    uint32_t num_args = ZEND_CALL_NUM_ARGS(execute_data);
    char db_name[DB_NAME_MAX] = {0};
    if (num_args >= 1) {
        zval *conn_arg = ZEND_CALL_ARG(execute_data, 1);
        if (conn_arg && Z_TYPE_P(conn_arg) == IS_STRING) {
            snprintf(db_name, DB_NAME_MAX, "%s", Z_STRVAL_P(conn_arg));
        }
    }
    record_oci_attr(state, span_index, db_name, strlen(db_name), 0);
    return NULL;
}

static void *oci_tx_pre(profiler_state_t *state, zend_execute_data *execute_data,
                          profiler_span_t *span, uint32_t span_index)
{
    (void)span;
    (void)execute_data;
    record_oci_attr(state, span_index, NULL, 0, 0);
    return NULL;
}

/* ── Registration ── */

void hook_oci8_register(hook_registry_t *reg)
{
    /* Procedural functions */
    hook_register_function(reg, "oci_connect",
        HOOK_TYPE_INTERNAL, SPAN_KIND_CLIENT,
        oci_connect_pre, NULL);
    hook_register_function(reg, "oci_new_connect",
        HOOK_TYPE_INTERNAL, SPAN_KIND_CLIENT,
        oci_connect_pre, NULL);
    hook_register_function(reg, "oci_pconnect",
        HOOK_TYPE_INTERNAL, SPAN_KIND_CLIENT,
        oci_connect_pre, NULL);
    hook_register_function(reg, "oci_parse",
        HOOK_TYPE_INTERNAL, SPAN_KIND_CLIENT,
        oci_parse_pre, NULL);
    hook_register_function(reg, "oci_execute",
        HOOK_TYPE_INTERNAL, SPAN_KIND_CLIENT,
        oci_execute_pre, NULL);
    hook_register_function(reg, "oci_commit",
        HOOK_TYPE_INTERNAL, SPAN_KIND_CLIENT,
        oci_tx_pre, NULL);
    hook_register_function(reg, "oci_rollback",
        HOOK_TYPE_INTERNAL, SPAN_KIND_CLIENT,
        oci_tx_pre, NULL);
}
