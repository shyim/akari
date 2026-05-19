#include "profiler_internal.h"
#include "hook_registry.h"

/*
 * gRPC instrumentation — hooks ext-grpc.
 *
 * Hooks Grpc\Call::__construct and Grpc\Call::startBatch.
 * startBatch is where actual RPCs are dispatched.
 * Records db.system=grpc with method/message type as db.statement.
 */

/* ── Attribute helper ── */

static void record_grpc_attr(profiler_state_t *state, uint32_t span_index,
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
    snprintf(attr->db_system, DB_SYSTEM_MAX, "grpc");
    if (statement && stmt_len > 0) {
        if (stmt_len >= DB_STATEMENT_MAX) stmt_len = DB_STATEMENT_MAX - 1;
        memcpy(attr->db_statement, statement, stmt_len);
        attr->db_statement[stmt_len] = '\0';
        attr->db_statement_len = stmt_len;
    }
    state->db_attr_count++;
}

/* ── Hooks ── */

static void *grpc_call_construct_pre(profiler_state_t *state, zend_execute_data *execute_data,
                                       profiler_span_t *span, uint32_t span_index)
{
    (void)span;
    uint32_t num_args = ZEND_CALL_NUM_ARGS(execute_data);
    char buf[256] = {0};
    size_t buf_len = 0;

    /* Arg 1: hostname, Arg 2: method name (/package.Service/Method) */
    if (num_args >= 2) {
        zval *method_arg = ZEND_CALL_ARG(execute_data, 2);
        if (method_arg && Z_TYPE_P(method_arg) == IS_STRING) {
            int n = snprintf(buf, sizeof(buf), "%s", Z_STRVAL_P(method_arg));
            buf_len = profiler_clamp_snprintf_len(n, sizeof(buf));
        }
    }

    record_grpc_attr(state, span_index, buf, buf_len);
    return NULL;
}

static void *grpc_start_batch_pre(profiler_state_t *state, zend_execute_data *execute_data,
                                    profiler_span_t *span, uint32_t span_index)
{
    (void)span;
    (void)execute_data;
    record_grpc_attr(state, span_index, NULL, 0);
    return NULL;
}

/* ── Registration ── */

void hook_grpc_register(hook_registry_t *reg)
{
    /* Grpc\Call class methods (internal class from ext-grpc) */
    hook_register_method(reg,
        "Grpc\\Call", "__construct",
        HOOK_TYPE_INTERNAL, SPAN_KIND_CLIENT, 0,
        grpc_call_construct_pre, NULL);

    hook_register_method(reg,
        "Grpc\\Call", "startbatch",
        HOOK_TYPE_INTERNAL, SPAN_KIND_CLIENT, 0,
        grpc_start_batch_pre, NULL);
}
