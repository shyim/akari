#include "profiler_internal.h"
#include "hook_registry.h"

/*
 * Pheanstalk (Beanstalkd client) instrumentation — userland hook.
 *
 * Hooks Pheanstalk\Connection::dispatchCommand() — the single entry point
 * for all Beanstalkd operations.
 * Records db.system=beanstalkd with command name as db.statement.
 */

/* ── Helper ── */

static void record_beanstalk_attr(profiler_state_t *state, uint32_t span_index,
                                    const char *statement, size_t stmt_len)
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
    snprintf(attr->db_system, DB_SYSTEM_MAX, "beanstalkd");
    if (statement && stmt_len > 0) {
        if (stmt_len >= DB_STATEMENT_MAX) stmt_len = DB_STATEMENT_MAX - 1;
        memcpy(attr->db_statement, statement, stmt_len);
        attr->db_statement[stmt_len] = '\0';
        attr->db_statement_len = stmt_len;
    }
    state->db_attr_count++;
}

/* ── Pre-hook ── */

static void *pheanstalk_pre(profiler_state_t *state, zend_execute_data *execute_data,
                              profiler_span_t *span, uint32_t span_index)
{
    (void)span;
    uint32_t num_args = ZEND_CALL_NUM_ARGS(execute_data);
    char buf[256] = "beanstalkd";
    size_t buf_len = 10;

    /* Arg 1: Command object — try to get its name */
    if (num_args >= 1) {
        zval *cmd_arg = ZEND_CALL_ARG(execute_data, 1);
        if (cmd_arg && Z_TYPE_P(cmd_arg) == IS_OBJECT) {
            zval rv;
            zval *name = zend_read_property_ex(Z_OBJCE_P(cmd_arg), Z_OBJ_P(cmd_arg),
                            ZSTR_KNOWN(ZEND_STR_NAME), 1, &rv);
            if (name && Z_TYPE_P(name) == IS_STRING) {
                int n = snprintf(buf, sizeof(buf), "%s", Z_STRVAL_P(name));
                buf_len = profiler_clamp_snprintf_len(n, sizeof(buf));
            }
        }
    }

    record_beanstalk_attr(state, span_index, buf, buf_len);
    return NULL;
}

/* ── Registration ── */

void hook_pheanstalk_register(hook_registry_t *reg)
{
    hook_register_method(reg,
        "Pheanstalk\\Connection", "dispatchcommand",
        HOOK_TYPE_USERLAND, SPAN_KIND_CLIENT, 0,
        pheanstalk_pre, NULL);
}
