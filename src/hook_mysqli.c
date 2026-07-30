#include "profiler_internal.h"
#include "hook_registry.h"

/*
 * MySQLi instrumentation — hooks both OO and procedural mysqli APIs.
 *
 * OO methods:
 *   mysqli::query(), mysqli::prepare(), mysqli_stmt::execute(),
 *   mysqli::begin_transaction(), mysqli::commit(), mysqli::rollback(),
 *   mysqli::multi_query(), mysqli::real_query(), mysqli::execute_query()
 *
 * Procedural functions:
 *   mysqli_query(), mysqli_prepare(), mysqli_stmt_execute(),
 *   mysqli_begin_transaction(), mysqli_commit(), mysqli_rollback(),
 *   mysqli_multi_query(), mysqli_real_query(), mysqli_execute_query()
 */

/* ── Helpers ── */

static void ensure_db_attr_capacity(profiler_state_t *state)
{
    if (state->db_attr_count < state->db_attr_capacity) return;
    size_t new_cap = state->db_attr_capacity ? state->db_attr_capacity * 2 : PROFILER_INITIAL_DB_ATTRS;
    profiler_db_attr_t *new_attrs = realloc(state->db_attrs, new_cap * sizeof(profiler_db_attr_t));
    if (!new_attrs) return;
    state->db_attrs = new_attrs;
    state->db_attr_capacity = new_cap;
}

/*
 * Record a db attr entry for a mysqli span.
 * sql_arg_pos: 1-based argument position of the SQL string (0 = no SQL arg).
 * For OO calls, arg 1 is the first method parameter.
 * For procedural calls, arg 1 is the link, arg 2+ are parameters.
 */
static void record_mysqli_attr(profiler_state_t *state, zend_execute_data *execute_data,
                                uint32_t span_index, int sql_arg_pos)
{
    ensure_db_attr_capacity(state);
    if (state->db_attr_count >= state->db_attr_capacity) return;

    profiler_db_attr_t *attr = &state->db_attrs[state->db_attr_count];
    memset(attr, 0, sizeof(profiler_db_attr_t));
    attr->span_index = span_index;
    strcpy(attr->db_system, "mysql");

    /* Extract SQL from argument */
    if (sql_arg_pos > 0) {
        uint32_t num_args = ZEND_CALL_NUM_ARGS(execute_data);
        if ((uint32_t)sql_arg_pos <= num_args) {
            zval *sql_arg = ZEND_CALL_ARG(execute_data, sql_arg_pos);
            if (sql_arg && Z_TYPE_P(sql_arg) == IS_STRING) {
                profiler_sql_normalize(
                    Z_STRVAL_P(sql_arg), Z_STRLEN_P(sql_arg),
                    attr->db_statement, sizeof(attr->db_statement),
                    &attr->db_statement_len);
            }
        }
    }

    state->db_attr_count++;
}

/* ── OO method callbacks ──
 * For OO calls: $mysqli->query($sql), first arg = $sql
 */

static void *mysqli_oo_query_pre(profiler_state_t *state, zend_execute_data *execute_data,
                                  profiler_span_t *span, uint32_t span_index)
{
    (void)span;
    record_mysqli_attr(state, execute_data, span_index, 1); /* arg 1 = SQL */
    return NULL;
}

static void *mysqli_oo_no_sql_pre(profiler_state_t *state, zend_execute_data *execute_data,
                                   profiler_span_t *span, uint32_t span_index)
{
    (void)span;
    record_mysqli_attr(state, execute_data, span_index, 0); /* no SQL arg */
    return NULL;
}

/* ── Procedural function callbacks ──
 * For procedural calls: mysqli_query($link, $sql), arg 1 = link, arg 2 = $sql
 */

static void *mysqli_proc_query_pre(profiler_state_t *state, zend_execute_data *execute_data,
                                    profiler_span_t *span, uint32_t span_index)
{
    (void)span;
    record_mysqli_attr(state, execute_data, span_index, 2); /* arg 2 = SQL */
    return NULL;
}

static void *mysqli_proc_no_sql_pre(profiler_state_t *state, zend_execute_data *execute_data,
                                     profiler_span_t *span, uint32_t span_index)
{
    (void)span;
    record_mysqli_attr(state, execute_data, span_index, 0);
    return NULL;
}

/* ── mysqli_stmt::execute — get SQL from statement ── */

static void *mysqli_stmt_execute_pre(profiler_state_t *state, zend_execute_data *execute_data,
                                      profiler_span_t *span, uint32_t span_index)
{
    (void)span;
    /* mysqli_stmt doesn't expose the SQL directly in a portable way.
     * Just record db.system = "mysql" without the statement. */
    record_mysqli_attr(state, execute_data, span_index, 0);
    return NULL;
}

/* ── Registration ── */

void hook_mysqli_register(hook_registry_t *reg)
{
    /* OO: mysqli class methods */
    hook_register_method(reg, "mysqli", "query",
        HOOK_TYPE_INTERNAL, SPAN_KIND_CLIENT, 1, mysqli_oo_query_pre, NULL);
    hook_register_method(reg, "mysqli", "real_query",
        HOOK_TYPE_INTERNAL, SPAN_KIND_CLIENT, 1, mysqli_oo_query_pre, NULL);
    hook_register_method(reg, "mysqli", "multi_query",
        HOOK_TYPE_INTERNAL, SPAN_KIND_CLIENT, 1, mysqli_oo_query_pre, NULL);
    hook_register_method(reg, "mysqli", "prepare",
        HOOK_TYPE_INTERNAL, SPAN_KIND_CLIENT, 1, mysqli_oo_query_pre, NULL);
    hook_register_method(reg, "mysqli", "execute_query",
        HOOK_TYPE_INTERNAL, SPAN_KIND_CLIENT, 1, mysqli_oo_query_pre, NULL);
    hook_register_method(reg, "mysqli", "begin_transaction",
        HOOK_TYPE_INTERNAL, SPAN_KIND_CLIENT, 1, mysqli_oo_no_sql_pre, NULL);
    hook_register_method(reg, "mysqli", "commit",
        HOOK_TYPE_INTERNAL, SPAN_KIND_CLIENT, 1, mysqli_oo_no_sql_pre, NULL);
    hook_register_method(reg, "mysqli", "rollback",
        HOOK_TYPE_INTERNAL, SPAN_KIND_CLIENT, 1, mysqli_oo_no_sql_pre, NULL);
    /* Additional OO methods */
    hook_register_method(reg, "mysqli", "__construct",
        HOOK_TYPE_INTERNAL, SPAN_KIND_CLIENT, 1, mysqli_oo_no_sql_pre, NULL);
    hook_register_method(reg, "mysqli", "real_connect",
        HOOK_TYPE_INTERNAL, SPAN_KIND_CLIENT, 1, mysqli_oo_no_sql_pre, NULL);
    hook_register_method(reg, "mysqli", "connect",
        HOOK_TYPE_INTERNAL, SPAN_KIND_CLIENT, 1, mysqli_oo_no_sql_pre, NULL);
    hook_register_method(reg, "mysqli", "savepoint",
        HOOK_TYPE_INTERNAL, SPAN_KIND_CLIENT, 1, mysqli_oo_query_pre, NULL);
    hook_register_method(reg, "mysqli", "release_savepoint",
        HOOK_TYPE_INTERNAL, SPAN_KIND_CLIENT, 1, mysqli_oo_query_pre, NULL);
    hook_register_method(reg, "mysqli", "select_db",
        HOOK_TYPE_INTERNAL, SPAN_KIND_CLIENT, 1, mysqli_oo_query_pre, NULL);

    /* OO: mysqli_stmt class */
    hook_register_method(reg, "mysqli_stmt", "execute",
        HOOK_TYPE_INTERNAL, SPAN_KIND_CLIENT, 1, mysqli_stmt_execute_pre, NULL);
    hook_register_method(reg, "mysqli_stmt", "prepare",
        HOOK_TYPE_INTERNAL, SPAN_KIND_CLIENT, 1, mysqli_stmt_execute_pre, NULL);

    /* Procedural functions */
    hook_register_function(reg, "mysqli_query",
        HOOK_TYPE_INTERNAL, SPAN_KIND_CLIENT, mysqli_proc_query_pre, NULL);
    hook_register_function(reg, "mysqli_real_query",
        HOOK_TYPE_INTERNAL, SPAN_KIND_CLIENT, mysqli_proc_query_pre, NULL);
    hook_register_function(reg, "mysqli_multi_query",
        HOOK_TYPE_INTERNAL, SPAN_KIND_CLIENT, mysqli_proc_query_pre, NULL);
    hook_register_function(reg, "mysqli_prepare",
        HOOK_TYPE_INTERNAL, SPAN_KIND_CLIENT, mysqli_proc_query_pre, NULL);
    hook_register_function(reg, "mysqli_execute_query",
        HOOK_TYPE_INTERNAL, SPAN_KIND_CLIENT, mysqli_proc_query_pre, NULL);
    hook_register_function(reg, "mysqli_stmt_execute",
        HOOK_TYPE_INTERNAL, SPAN_KIND_CLIENT, mysqli_stmt_execute_pre, NULL);
    hook_register_function(reg, "mysqli_stmt_prepare",
        HOOK_TYPE_INTERNAL, SPAN_KIND_CLIENT, mysqli_proc_query_pre, NULL);
    hook_register_function(reg, "mysqli_begin_transaction",
        HOOK_TYPE_INTERNAL, SPAN_KIND_CLIENT, mysqli_proc_no_sql_pre, NULL);
    hook_register_function(reg, "mysqli_commit",
        HOOK_TYPE_INTERNAL, SPAN_KIND_CLIENT, mysqli_proc_no_sql_pre, NULL);
    hook_register_function(reg, "mysqli_rollback",
        HOOK_TYPE_INTERNAL, SPAN_KIND_CLIENT, mysqli_proc_no_sql_pre, NULL);
    /* Additional procedural functions */
    hook_register_function(reg, "mysqli_connect",
        HOOK_TYPE_INTERNAL, SPAN_KIND_CLIENT, mysqli_proc_no_sql_pre, NULL);
    hook_register_function(reg, "mysqli_real_connect",
        HOOK_TYPE_INTERNAL, SPAN_KIND_CLIENT, mysqli_proc_no_sql_pre, NULL);
    hook_register_function(reg, "mysqli_savepoint",
        HOOK_TYPE_INTERNAL, SPAN_KIND_CLIENT, mysqli_proc_query_pre, NULL);
    hook_register_function(reg, "mysqli_release_savepoint",
        HOOK_TYPE_INTERNAL, SPAN_KIND_CLIENT, mysqli_proc_query_pre, NULL);
    hook_register_function(reg, "mysqli_select_db",
        HOOK_TYPE_INTERNAL, SPAN_KIND_CLIENT, mysqli_proc_query_pre, NULL);
    hook_register_function(reg, "mysqli_execute",
        HOOK_TYPE_INTERNAL, SPAN_KIND_CLIENT, mysqli_stmt_execute_pre, NULL);
}
