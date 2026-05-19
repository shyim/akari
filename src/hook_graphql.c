#include "profiler_internal.h"
#include "hook_registry.h"

/*
 * GraphQL instrumentation — userland hooks for webonyx/graphql-php library.
 *
 * Hooks:
 *   - GraphQL\GraphQL::executeQuery() — the main entry point
 *   - GraphQL\Executor\Executor::executeFields() — field resolver execution
 *   - GraphQL\Executor\Executor::resolveOrError() — individual resolver
 *   - GraphQL\Parser::parse() — query parsing
 *
 * Records db.system=graphql with query or field info as db.statement.
 */

/* ── Helper ── */

static void record_graphql_attr(profiler_state_t *state, uint32_t span_index,
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
    snprintf(attr->db_system, DB_SYSTEM_MAX, "graphql");
    if (statement && stmt_len > 0) {
        if (stmt_len >= DB_STATEMENT_MAX) stmt_len = DB_STATEMENT_MAX - 1;
        memcpy(attr->db_statement, statement, stmt_len);
        attr->db_statement[stmt_len] = '\0';
        attr->db_statement_len = stmt_len;
    }
    state->db_attr_count++;
}

/* ── executeQuery pre-hook ── */

static void *graphql_execute_pre(profiler_state_t *state, zend_execute_data *execute_data,
                                   profiler_span_t *span, uint32_t span_index)
{
    (void)span;
    uint32_t num_args = ZEND_CALL_NUM_ARGS(execute_data);
    char buf[1024] = {0};
    size_t buf_len = 0;

    if (num_args >= 3) {
        /* Arg 3: source string (the GraphQL query) */
        zval *source_arg = ZEND_CALL_ARG(execute_data, 3);
        if (source_arg && Z_TYPE_P(source_arg) == IS_STRING && Z_STRLEN_P(source_arg) > 0) {
            const char *src = Z_STRVAL_P(source_arg);
            size_t src_len = Z_STRLEN_P(source_arg);
            if (src_len > 200) src_len = 200;
            int n = snprintf(buf, sizeof(buf), "%.*s", (int)src_len, src);
            buf_len = profiler_clamp_snprintf_len(n, sizeof(buf));
        }
    }

    if (buf_len == 0) {
        int n = snprintf(buf, sizeof(buf), "executeQuery");
        buf_len = profiler_clamp_snprintf_len(n, sizeof(buf));
    }

    record_graphql_attr(state, span_index, buf, buf_len);
    return NULL;
}

/* ── Parser::parse — query parsing ── */

static void *graphql_parse_pre(profiler_state_t *state, zend_execute_data *execute_data,
                                 profiler_span_t *span, uint32_t span_index)
{
    (void)span;
    (void)execute_data;
    record_graphql_attr(state, span_index, "parse", 5);
    return NULL;
}

/* ── Executor field hooks ── */

static void *graphql_executor_pre(profiler_state_t *state, zend_execute_data *execute_data,
                                    profiler_span_t *span, uint32_t span_index)
{
    (void)span;
    const char *label = "executor";
    if (execute_data && execute_data->func && execute_data->func->common.function_name) {
        label = ZSTR_VAL(execute_data->func->common.function_name);
    }
    record_graphql_attr(state, span_index, label, strlen(label));
    return NULL;
}

/* ── Registration ── */

void hook_graphql_register(hook_registry_t *reg)
{
    /* Main entry point */
    hook_register_method(reg,
        "GraphQL\\GraphQL", "executequery",
        HOOK_TYPE_USERLAND, SPAN_KIND_INTERNAL, 0,
        graphql_execute_pre, NULL);

    /* Parser */
    hook_register_method(reg,
        "GraphQL\\Parser", "parse",
        HOOK_TYPE_USERLAND, SPAN_KIND_INTERNAL, 0,
        graphql_parse_pre, NULL);

    /* Executor */
    hook_register_method(reg,
        "GraphQL\\Executor\\Executor", "executefields",
        HOOK_TYPE_USERLAND, SPAN_KIND_INTERNAL, 0,
        graphql_executor_pre, NULL);

    hook_register_method(reg,
        "GraphQL\\Executor\\Executor", "resolveorerror",
        HOOK_TYPE_USERLAND, SPAN_KIND_INTERNAL, 0,
        graphql_executor_pre, NULL);
}
