#include "profiler_internal.h"
#include "hook_registry.h"

/*
 * AMQP instrumentation — hooks both ext-amqp and php-amqplib.
 *
 * ext-amqp:
 *   AMQPExchange::publish    → PRODUCER, injects traceparent
 *   AMQPQueue::get           → CONSUMER, extracts traceparent link
 *   AMQPQueue::consume       → CONSUMER
 *   AMQPConnection::connect  → CLIENT
 *   AMQPChannel::__construct → CLIENT
 *
 * php-amqplib:
 *   AbstractConnection::channel   → INTERNAL
 *   AbstractConnection::connect   → CLIENT
 *   AMQPChannel::basic_publish    → PRODUCER
 *   AbstractIO::read/write        → CLIENT (network I/O)
 */

/* ── Attribute capacity helper ── */

static void ensure_msg_attr_capacity(profiler_state_t *state)
{
    if (state->msg_attr_count < state->msg_attr_capacity) return;
    size_t new_cap = state->msg_attr_capacity ? state->msg_attr_capacity * 2 : 16;
    profiler_messaging_attr_t *new_attrs = realloc(state->msg_attrs,
                                                    new_cap * sizeof(profiler_messaging_attr_t));
    if (!new_attrs) return;
    state->msg_attrs = new_attrs;
    state->msg_attr_capacity = new_cap;
}

/* ── Traceparent extraction from consumed message ── */

static int extract_traceparent_from_envelope(zval *return_value,
                                              char *trace_id_out,
                                              char *span_id_out)
{
    if (!return_value || Z_TYPE_P(return_value) != IS_OBJECT) return 0;

    zval method_name, arg, retval;
    ZVAL_STRING(&method_name, "getHeader");
    ZVAL_STRING(&arg, "traceparent");

    zval params[1];
    params[0] = arg;

    int result = call_user_function(NULL, return_value, &method_name, &retval, 1, params);

    zval_ptr_dtor(&method_name);
    zval_ptr_dtor(&arg);

    if (result != SUCCESS || Z_TYPE(retval) != IS_STRING) {
        zval_ptr_dtor(&retval);
        return 0;
    }

    /* Parse traceparent: 00-{32hex}-{16hex}-{2hex} */
    const char *tp = Z_STRVAL(retval);
    size_t tp_len = Z_STRLEN(retval);

    if (tp_len < 55 || tp[2] != '-' || tp[35] != '-' || tp[52] != '-') {
        zval_ptr_dtor(&retval);
        return 0;
    }

    memcpy(trace_id_out, tp + 3, 32);
    memcpy(span_id_out, tp + 36, 16);

    zval_ptr_dtor(&retval);
    return 1;
}

/* ── Simple messaging attr recorder (no traceparent injection) ── */

static void record_msg_attr_simple(profiler_state_t *state, uint32_t span_index,
                                    const char *system, const char *operation)
{
    ensure_msg_attr_capacity(state);
    if (state->msg_attr_count >= state->msg_attr_capacity) return;

    profiler_messaging_attr_t *attr = &state->msg_attrs[state->msg_attr_count];
    memset(attr, 0, sizeof(profiler_messaging_attr_t));
    attr->span_index = span_index;
    snprintf(attr->messaging_system, sizeof(attr->messaging_system), "%s", system);
    snprintf(attr->messaging_operation, sizeof(attr->messaging_operation), "%s", operation);
    state->msg_attr_count++;
}

/* ── ext-amqp: Publish callback ── */

static void *amqp_publish_pre(profiler_state_t *state, zend_execute_data *execute_data,
                               profiler_span_t *span, uint32_t span_index)
{
    /* Inject traceparent into $attributes['headers']['traceparent'] */
    uint32_t num_args = ZEND_CALL_NUM_ARGS(execute_data);
    if (num_args >= 4) {
        zval *attrs_arg = ZEND_CALL_ARG(execute_data, 4);
        if (Z_TYPE_P(attrs_arg) == IS_ARRAY) {
            char traceparent[128];
            snprintf(traceparent, sizeof(traceparent), "00-%.32s-%.16s-01",
                     state->trace_id, span->span_id);

            zval *headers_zv = zend_hash_str_find(Z_ARRVAL_P(attrs_arg), "headers", 7);
            if (headers_zv && Z_TYPE_P(headers_zv) == IS_ARRAY) {
                SEPARATE_ARRAY(headers_zv);
                add_assoc_string(headers_zv, "traceparent", traceparent);
            } else {
                zval new_headers;
                array_init(&new_headers);
                add_assoc_string(&new_headers, "traceparent", traceparent);
                add_assoc_zval(attrs_arg, "headers", &new_headers);
            }
        }
    }

    /* Record publish attributes */
    ensure_msg_attr_capacity(state);
    if (state->msg_attr_count >= state->msg_attr_capacity) return NULL;

    profiler_messaging_attr_t *attr = &state->msg_attrs[state->msg_attr_count];
    memset(attr, 0, sizeof(profiler_messaging_attr_t));
    attr->span_index = span_index;
    strcpy(attr->messaging_system, "rabbitmq");
    strcpy(attr->messaging_operation, "publish");

    if (num_args >= 2) {
        zval *routing_key = ZEND_CALL_ARG(execute_data, 2);
        if (routing_key && Z_TYPE_P(routing_key) == IS_STRING) {
            snprintf(attr->destination_name, sizeof(attr->destination_name),
                     "%s", Z_STRVAL_P(routing_key));
        }
    }

    state->msg_attr_count++;
    return NULL;
}

/* ── ext-amqp: Consume callback ── */

static void amqp_consume_post(profiler_state_t *state, zend_execute_data *execute_data,
                               zval *return_value, profiler_span_t *span,
                               uint32_t span_index, void *pre_data)
{
    (void)span;
    (void)pre_data;

    ensure_msg_attr_capacity(state);
    if (state->msg_attr_count >= state->msg_attr_capacity) return;

    profiler_messaging_attr_t *attr = &state->msg_attrs[state->msg_attr_count];
    memset(attr, 0, sizeof(profiler_messaging_attr_t));
    attr->span_index = span_index;
    strcpy(attr->messaging_system, "rabbitmq");
    strcpy(attr->messaging_operation, "receive");

    /* Extract queue name from $this (AMQPQueue) */
    zval *this_obj = &execute_data->This;
    if (Z_TYPE_P(this_obj) == IS_OBJECT) {
        zval method_name, retval;
        ZVAL_STRING(&method_name, "getName");
        if (call_user_function(NULL, this_obj, &method_name, &retval, 0, NULL) == SUCCESS) {
            if (Z_TYPE(retval) == IS_STRING) {
                snprintf(attr->destination_name, sizeof(attr->destination_name),
                         "%s", Z_STRVAL(retval));
            }
            zval_ptr_dtor(&retval);
        }
        zval_ptr_dtor(&method_name);
    }

    /* Extract traceparent from envelope for linking */
    if (return_value && Z_TYPE_P(return_value) == IS_OBJECT) {
        attr->has_link = extract_traceparent_from_envelope(
            return_value, attr->linked_trace_id, attr->linked_span_id);
    }

    state->msg_attr_count++;
}

/* ── ext-amqp: connection/channel pre-hook ── */

static void *amqp_connect_pre(profiler_state_t *state, zend_execute_data *execute_data,
                                profiler_span_t *span, uint32_t span_index)
{
    (void)execute_data;
    (void)span;
    record_msg_attr_simple(state, span_index, "rabbitmq", "connect");
    return NULL;
}

/* ── php-amqplib: basic_publish pre-hook ── */

static void *amqplib_publish_pre(profiler_state_t *state, zend_execute_data *execute_data,
                                   profiler_span_t *span, uint32_t span_index)
{
    (void)span;
    ensure_msg_attr_capacity(state);
    if (state->msg_attr_count >= state->msg_attr_capacity) return NULL;

    profiler_messaging_attr_t *attr = &state->msg_attrs[state->msg_attr_count];
    memset(attr, 0, sizeof(profiler_messaging_attr_t));
    attr->span_index = span_index;
    strcpy(attr->messaging_system, "rabbitmq");
    strcpy(attr->messaging_operation, "publish");

    /* basic_publish($msg, $exchange='', $routing_key='', ...) — extract exchange (arg 2) */
    uint32_t num_args = ZEND_CALL_NUM_ARGS(execute_data);
    if (num_args >= 2) {
        zval *exchange_arg = ZEND_CALL_ARG(execute_data, 2);
        if (exchange_arg && Z_TYPE_P(exchange_arg) == IS_STRING && Z_STRLEN_P(exchange_arg) > 0) {
            snprintf(attr->destination_name, sizeof(attr->destination_name),
                     "%s", Z_STRVAL_P(exchange_arg));
        }
    }
    /* If no exchange, try routing_key (arg 3) */
    if (attr->destination_name[0] == '\0' && num_args >= 3) {
        zval *rk_arg = ZEND_CALL_ARG(execute_data, 3);
        if (rk_arg && Z_TYPE_P(rk_arg) == IS_STRING && Z_STRLEN_P(rk_arg) > 0) {
            snprintf(attr->destination_name, sizeof(attr->destination_name),
                     "%s", Z_STRVAL_P(rk_arg));
        }
    }

    state->msg_attr_count++;
    return NULL;
}

/* ── php-amqplib: IO pre-hook ── */

static void *amqplib_io_pre(profiler_state_t *state, zend_execute_data *execute_data,
                              profiler_span_t *span, uint32_t span_index)
{
    (void)execute_data;
    (void)span;
    record_msg_attr_simple(state, span_index, "rabbitmq", "io");
    return NULL;
}

/* ── Registration ── */

void hook_amqp_register(hook_registry_t *reg)
{
    /* ── ext-amqp ── */

    hook_register_method(reg, "AMQPExchange", "publish",
        HOOK_TYPE_INTERNAL, SPAN_KIND_PRODUCER, 1, amqp_publish_pre, NULL);
    hook_register_method(reg, "AMQPQueue", "get",
        HOOK_TYPE_INTERNAL, SPAN_KIND_CONSUMER, 1, NULL, amqp_consume_post);
    hook_register_method(reg, "AMQPQueue", "consume",
        HOOK_TYPE_INTERNAL, SPAN_KIND_CONSUMER, 1, NULL, NULL);
    hook_register_method(reg, "AMQPConnection", "connect",
        HOOK_TYPE_INTERNAL, SPAN_KIND_CLIENT, 1, amqp_connect_pre, NULL);
    hook_register_method(reg, "AMQPConnection", "pconnect",
        HOOK_TYPE_INTERNAL, SPAN_KIND_CLIENT, 1, amqp_connect_pre, NULL);
    hook_register_method(reg, "AMQPChannel", "__construct",
        HOOK_TYPE_INTERNAL, SPAN_KIND_CLIENT, 1, amqp_connect_pre, NULL);

    /* ── php-amqplib (userland) ── */

    /* AbstractConnection — connect and channel creation */
    hook_register_method(reg,
        "PhpAmqpLib\\Connection\\AbstractConnection", "connect",
        HOOK_TYPE_USERLAND, SPAN_KIND_CLIENT, 1,
        NULL, NULL);
    hook_register_method(reg,
        "PhpAmqpLib\\Connection\\AbstractConnection", "channel",
        HOOK_TYPE_USERLAND, SPAN_KIND_INTERNAL, 1,
        NULL, NULL);

    /* AMQPChannel::basic_publish — message publishing */
    hook_register_method(reg,
        "PhpAmqpLib\\Channel\\AMQPChannel", "basic_publish",
        HOOK_TYPE_USERLAND, SPAN_KIND_PRODUCER, 1,
        amqplib_publish_pre, NULL);
    hook_register_method(reg,
        "PhpAmqpLib\\Channel\\AMQPChannel", "basic_consume",
        HOOK_TYPE_USERLAND, SPAN_KIND_CONSUMER, 1,
        NULL, NULL);
    hook_register_method(reg,
        "PhpAmqpLib\\Channel\\AMQPChannel", "basic_get",
        HOOK_TYPE_USERLAND, SPAN_KIND_CONSUMER, 1,
        NULL, NULL);

    /* IO layer — network reads/writes */
    hook_register_method(reg,
        "PhpAmqpLib\\Wire\\IO\\AbstractIO", "read",
        HOOK_TYPE_USERLAND, SPAN_KIND_CLIENT, 1,
        amqplib_io_pre, NULL);
    hook_register_method(reg,
        "PhpAmqpLib\\Wire\\IO\\AbstractIO", "write",
        HOOK_TYPE_USERLAND, SPAN_KIND_CLIENT, 1,
        amqplib_io_pre, NULL);
}
