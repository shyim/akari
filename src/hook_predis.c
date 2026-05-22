#include "profiler_internal.h"
#include "hook_registry.h"

/*
 * Predis instrumentation — userland hook for the pure-PHP Redis client.
 *
 * Hooks Predis\Client::executeCommand() which is the single entry point
 * for all Redis commands in Predis.
 *
 * Extracts command name via CommandInterface::getId() and the first key
 * via CommandInterface::getArgument(0) from the first argument.
 */

static void *predis_pre(profiler_state_t *state, zend_execute_data *execute_data,
                         profiler_span_t *span, uint32_t span_index)
{
    (void)span;

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
    strcpy(attr->db_system, "redis");

    /* Extract command name and key from the CommandInterface argument */
    uint32_t num_args = ZEND_CALL_NUM_ARGS(execute_data);
    if (num_args >= 1) {
        zval *cmd_arg = ZEND_CALL_ARG(execute_data, 1);
        if (cmd_arg && Z_TYPE_P(cmd_arg) == IS_OBJECT) {
            /* Call $command->getId() to get command name (e.g. "GET", "SET") */
            zval method_name, retval;
            ZVAL_STRING(&method_name, "getId");
            if (call_user_function(NULL, cmd_arg, &method_name, &retval, 0, NULL) == SUCCESS) {
                if (Z_TYPE(retval) == IS_STRING && Z_STRLEN(retval) > 0) {
                    /* Try to get the first argument (key) via $command->getArgument(0) */
                    zval get_arg_method, arg_idx, arg_retval;
                    ZVAL_STRING(&get_arg_method, "getArgument");
                    ZVAL_LONG(&arg_idx, 0);
                    zval params[1];
                    params[0] = arg_idx;

                    if (call_user_function(NULL, cmd_arg, &get_arg_method, &arg_retval, 1, params) == SUCCESS) {
                        if (Z_TYPE(arg_retval) == IS_STRING && Z_STRLEN(arg_retval) > 0) {
                            int n = snprintf(attr->db_statement, DB_STATEMENT_MAX,
                                "%s %s", Z_STRVAL(retval), Z_STRVAL(arg_retval));
                            attr->db_statement_len = (n > 0 && (size_t)n < DB_STATEMENT_MAX) ? (size_t)n : DB_STATEMENT_MAX - 1;
                        } else {
                            int n = snprintf(attr->db_statement, DB_STATEMENT_MAX,
                                "%s", Z_STRVAL(retval));
                            attr->db_statement_len = (n > 0 && (size_t)n < DB_STATEMENT_MAX) ? (size_t)n : DB_STATEMENT_MAX - 1;
                        }
                        zval_ptr_dtor(&arg_retval);
                    } else {
                        int n = snprintf(attr->db_statement, DB_STATEMENT_MAX,
                            "%s", Z_STRVAL(retval));
                        attr->db_statement_len = (n > 0 && (size_t)n < DB_STATEMENT_MAX) ? (size_t)n : DB_STATEMENT_MAX - 1;
                    }
                    zval_ptr_dtor(&get_arg_method);
                }
                zval_ptr_dtor(&retval);
            }
            zval_ptr_dtor(&method_name);
        }
    }

    state->db_attr_count++;
    return NULL;
}

/* ── Registration ── */

void hook_predis_register(hook_registry_t *reg)
{
    hook_register_method(reg,
        "Predis\\Client", "executeCommand",
        HOOK_TYPE_USERLAND, SPAN_KIND_CLIENT, 1,
        predis_pre, NULL);
}
