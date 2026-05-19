#include "profiler_internal.h"
#include "hook_registry.h"

/*
 * SOAP client instrumentation — hooks ext-soap.
 *
 * Hooks SoapClient::__doRequest() — the single entry point for all SOAP operations.
 * Records db.system=soap and db.statement with the request URI.
 */

/* ── Helper ── */

static void record_soap_attr(profiler_state_t *state, uint32_t span_index,
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
    snprintf(attr->db_system, DB_SYSTEM_MAX, "soap");
    if (statement && stmt_len > 0) {
        if (stmt_len >= DB_STATEMENT_MAX) stmt_len = DB_STATEMENT_MAX - 1;
        memcpy(attr->db_statement, statement, stmt_len);
        attr->db_statement[stmt_len] = '\0';
        attr->db_statement_len = stmt_len;
    }
    state->db_attr_count++;
}

/* ── Pre-hook: __doRequest(string $request, string $location, string $action, int $version) ── */

static void *soap_do_request_pre(profiler_state_t *state, zend_execute_data *execute_data,
                                   profiler_span_t *span, uint32_t span_index)
{
    (void)span;
    uint32_t num_args = ZEND_CALL_NUM_ARGS(execute_data);
    char buf[2048] = {0};
    size_t buf_len = 0;

    /* Arg 2: location/endpoint URL */
    if (num_args >= 2) {
        zval *location_arg = ZEND_CALL_ARG(execute_data, 2);
        if (location_arg && Z_TYPE_P(location_arg) == IS_STRING && Z_STRLEN_P(location_arg) > 0) {
            buf_len = snprintf(buf, sizeof(buf), "%s", Z_STRVAL_P(location_arg));
        }
    }

    /* Arg 3: SOAP action */
    if (num_args >= 3 && buf_len < sizeof(buf) - 1) {
        zval *action_arg = ZEND_CALL_ARG(execute_data, 3);
        if (action_arg && Z_TYPE_P(action_arg) == IS_STRING && Z_STRLEN_P(action_arg) > 0) {
            snprintf(buf + buf_len, sizeof(buf) - buf_len, " [%s]", Z_STRVAL_P(action_arg));
            buf_len = strlen(buf);
        }
    }

    record_soap_attr(state, span_index, buf, buf_len);
    return NULL;
}

/* ── Registration ── */

void hook_soap_register(hook_registry_t *reg)
{
    hook_register_method(reg,
        "SoapClient", "__dorequest",
        HOOK_TYPE_INTERNAL, SPAN_KIND_CLIENT, 0,
        soap_do_request_pre, NULL);
}
