#include "profiler_internal.h"
#include "hook_registry.h"

/*
 * rdkafka instrumentation — hooks ext-rdkafka.
 *
 * Hooks RdKafka\Producer methods:
 *   - __construct
 *   - flush, poll, initTransactions, commitTransaction, abortTransaction
 *   - RdKafka\ProducerTopic::produce, producev
 *
 * Records db.system=kafka with topic as db.statement.
 */

/* ── Attribute helper ── */

static void record_kafka_attr(profiler_state_t *state, uint32_t span_index,
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
    snprintf(attr->db_system, DB_SYSTEM_MAX, "kafka");
    if (statement && stmt_len > 0) {
        if (stmt_len >= DB_STATEMENT_MAX) stmt_len = DB_STATEMENT_MAX - 1;
        memcpy(attr->db_statement, statement, stmt_len);
        attr->db_statement[stmt_len] = '\0';
        attr->db_statement_len = stmt_len;
    }
    state->db_attr_count++;
}

/* ── Producer::__construct — records config ── */

static void *kafka_producer_construct_pre(profiler_state_t *state, zend_execute_data *execute_data,
                                            profiler_span_t *span, uint32_t span_index)
{
    (void)span;
    (void)execute_data;
    record_kafka_attr(state, span_index, NULL, 0);
    return NULL;
}

/* ── ProducerTopic::produce — records topic name (arg 2) ── */

static void *kafka_produce_pre(profiler_state_t *state, zend_execute_data *execute_data,
                                 profiler_span_t *span, uint32_t span_index)
{
    (void)span;
    uint32_t num_args = ZEND_CALL_NUM_ARGS(execute_data);
    char buf[256] = {0};
    size_t buf_len = 0;

    if (num_args >= 2) {
        zval *topic_arg = ZEND_CALL_ARG(execute_data, 2);
        if (topic_arg && Z_TYPE_P(topic_arg) == IS_STRING) {
            buf_len = snprintf(buf, sizeof(buf), "produce %s", Z_STRVAL_P(topic_arg));
        }
    }

    if (buf_len == 0) {
        buf_len = snprintf(buf, sizeof(buf), "produce");
    }

    record_kafka_attr(state, span_index, buf, buf_len);
    return NULL;
}

/* ── Generic Kafka operation ── */

static void *kafka_op_pre(profiler_state_t *state, zend_execute_data *execute_data,
                            profiler_span_t *span, uint32_t span_index)
{
    (void)span;
    const char *method = "kafka";
    if (execute_data && execute_data->func && execute_data->func->common.function_name) {
        method = ZSTR_VAL(execute_data->func->common.function_name);
    }
    record_kafka_attr(state, span_index, method, strlen(method));
    return NULL;
}

/* ── Registration ── */

void hook_rdkafka_register(hook_registry_t *reg)
{
    /* RdKafka\Producer — lifecycle and transaction methods */
    hook_register_method(reg,
        "RdKafka\\Producer", "__construct",
        HOOK_TYPE_INTERNAL, SPAN_KIND_CLIENT, 0,
        kafka_producer_construct_pre, NULL);

    hook_register_method(reg,
        "RdKafka\\Producer", "flush",
        HOOK_TYPE_INTERNAL, SPAN_KIND_CLIENT, 0,
        kafka_op_pre, NULL);

    hook_register_method(reg,
        "RdKafka\\Producer", "poll",
        HOOK_TYPE_INTERNAL, SPAN_KIND_INTERNAL, 0,
        kafka_op_pre, NULL);

    hook_register_method(reg,
        "RdKafka\\Producer", "inittransactions",
        HOOK_TYPE_INTERNAL, SPAN_KIND_INTERNAL, 0,
        kafka_op_pre, NULL);

    hook_register_method(reg,
        "RdKafka\\Producer", "committransaction",
        HOOK_TYPE_INTERNAL, SPAN_KIND_INTERNAL, 0,
        kafka_op_pre, NULL);

    hook_register_method(reg,
        "RdKafka\\Producer", "aborttransaction",
        HOOK_TYPE_INTERNAL, SPAN_KIND_INTERNAL, 0,
        kafka_op_pre, NULL);

    /* RdKafka\ProducerTopic — produce operations */
    hook_register_method(reg,
        "RdKafka\\ProducerTopic", "produce",
        HOOK_TYPE_INTERNAL, SPAN_KIND_PRODUCER, 0,
        kafka_produce_pre, NULL);

    hook_register_method(reg,
        "RdKafka\\ProducerTopic", "producev",
        HOOK_TYPE_INTERNAL, SPAN_KIND_PRODUCER, 0,
        kafka_produce_pre, NULL);
}
