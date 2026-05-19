#include "../include/php_akari.h"
#include "ext/standard/info.h"
#include "profiler.h"
#include "profiler_internal.h"
#include "otlp_export.h"
#include "udp_export.h"
#include "Zend/zend_exceptions.h"

/* Module globals */
ZEND_BEGIN_MODULE_GLOBALS(akari)
    zend_bool enable;
    char *service_name;
    zend_long max_depth;
    zend_bool trace_internal;
    zend_bool trace_functions;
    char *mode;
    double sample_period;
    double min_duration_ms;
    char *udp_host;
    zend_long udp_port;
    zend_bool trace_compile;
    zend_bool trace_gc;
ZEND_END_MODULE_GLOBALS(akari)

ZEND_DECLARE_MODULE_GLOBALS(akari)

#define AKARI_G(v) ZEND_MODULE_GLOBALS_ACCESSOR(akari, v)

/* INI entries */
PHP_INI_BEGIN()
    STD_PHP_INI_BOOLEAN("akari.enable", "0", PHP_INI_SYSTEM, OnUpdateBool, enable, zend_akari_globals, akari_globals)
    STD_PHP_INI_ENTRY("akari.service_name", "php", PHP_INI_SYSTEM, OnUpdateString, service_name, zend_akari_globals, akari_globals)
    STD_PHP_INI_ENTRY("akari.max_depth", "64", PHP_INI_SYSTEM, OnUpdateLong, max_depth, zend_akari_globals, akari_globals)
    STD_PHP_INI_BOOLEAN("akari.trace_internal", "0", PHP_INI_SYSTEM, OnUpdateBool, trace_internal, zend_akari_globals, akari_globals)
    STD_PHP_INI_BOOLEAN("akari.trace_functions", "0", PHP_INI_SYSTEM, OnUpdateBool, trace_functions, zend_akari_globals, akari_globals)
    STD_PHP_INI_ENTRY("akari.mode", "trace", PHP_INI_SYSTEM, OnUpdateString, mode, zend_akari_globals, akari_globals)
    STD_PHP_INI_ENTRY("akari.sample_period", "0.01", PHP_INI_SYSTEM, OnUpdateReal, sample_period, zend_akari_globals, akari_globals)
    STD_PHP_INI_ENTRY("akari.min_duration_ms", "0", PHP_INI_SYSTEM, OnUpdateReal, min_duration_ms, zend_akari_globals, akari_globals)
    STD_PHP_INI_ENTRY("akari.udp_host", "127.0.0.1", PHP_INI_SYSTEM, OnUpdateString, udp_host, zend_akari_globals, akari_globals)
    STD_PHP_INI_ENTRY("akari.udp_port", "4319", PHP_INI_SYSTEM, OnUpdateLong, udp_port, zend_akari_globals, akari_globals)
    STD_PHP_INI_BOOLEAN("akari.trace_compile", "0", PHP_INI_SYSTEM, OnUpdateBool, trace_compile, zend_akari_globals, akari_globals)
    STD_PHP_INI_BOOLEAN("akari.trace_gc", "0", PHP_INI_SYSTEM, OnUpdateBool, trace_gc, zend_akari_globals, akari_globals)
PHP_INI_END()

static PHP_GINIT_FUNCTION(akari)
{
#if defined(ZEND_COMPILE_DL_EXT) && defined(ZTS)
    ZEND_TSRMLS_CACHE_UPDATE();
#endif
    akari_globals->enable = 0;
    akari_globals->service_name = NULL;
    akari_globals->max_depth = 64;
    akari_globals->trace_internal = 0;
    akari_globals->trace_functions = 0;
    akari_globals->mode = NULL;
    akari_globals->sample_period = 0.01;
    akari_globals->min_duration_ms = 0;
    akari_globals->udp_host = NULL;
    akari_globals->udp_port = 4319;
    akari_globals->trace_compile = 0;
    akari_globals->trace_gc = 0;
}

/* Export: always UDP */
static void export_spans(profiler_state_t *state)
{
    if (!state || state->span_count == 0) return;
    const char *service_name = AKARI_G(service_name);
    if (!service_name) return;
    udp_export_spans(state, service_name);
}

/* Flush callback */
static void flush_callback(profiler_state_t *state, void *user_data)
{
    (void)user_data;
    export_spans(state);
}

static int get_tracer_mode(void)
{
    const char *mode = AKARI_G(mode);
    if (mode && strcmp(mode, "sample") == 0) {
        return PROFILER_MODE_SAMPLE;
    }
    return PROFILER_MODE_TRACE;
}

/* Arginfo */
ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_enable, 0, 0, _IS_BOOL, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_disable, 0, 0, _IS_BOOL, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_get_span_count, 0, 0, IS_LONG, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_get_frame_count, 0, 0, IS_LONG, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_get_spans_json, 0, 0, IS_STRING, 1)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_set_transaction_name, 0, 1, IS_VOID, 0)
    ZEND_ARG_TYPE_INFO(0, name, IS_STRING, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_get_transaction_name, 0, 0, IS_STRING, 1)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_set_service_name, 0, 1, IS_VOID, 0)
    ZEND_ARG_TYPE_INFO(0, name, IS_STRING, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_add_tag, 0, 2, IS_VOID, 0)
    ZEND_ARG_TYPE_INFO(0, key, IS_STRING, 0)
    ZEND_ARG_TYPE_INFO(0, value, IS_STRING, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_get_tags, 0, 0, IS_ARRAY, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_remove_tag, 0, 1, IS_VOID, 0)
    ZEND_ARG_TYPE_INFO(0, key, IS_STRING, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_set_custom_var, 0, 2, IS_VOID, 0)
    ZEND_ARG_TYPE_INFO(0, key, IS_STRING, 0)
    ZEND_ARG_TYPE_INFO(0, value, IS_STRING, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_create_span, 0, 1, IS_STRING, 1)
    ZEND_ARG_TYPE_INFO(0, name, IS_STRING, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_log_exception, 0, 1, IS_VOID, 0)
    ZEND_ARG_OBJ_INFO(0, exception, Throwable, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_distributed_headers, 0, 0, IS_ARRAY, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_mark_as_web, 0, 0, IS_VOID, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_mark_as_cli, 0, 0, IS_VOID, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_is_profiling, 0, 0, _IS_BOOL, 0)
ZEND_END_ARG_INFO()

/* Namespaced PHP functions */

ZEND_NAMED_FUNCTION(zf_akari_enable)
{
    ZEND_PARSE_PARAMETERS_NONE();
    int mode = get_tracer_mode();
    profiler_rinit(mode,
        (uint32_t)AKARI_G(max_depth),
        AKARI_G(trace_internal),
        AKARI_G(trace_functions),
        AKARI_G(sample_period),
        AKARI_G(min_duration_ms));
    profiler_set_flush_callback(flush_callback, NULL);
    RETURN_TRUE;
}

ZEND_NAMED_FUNCTION(zf_akari_disable)
{
    ZEND_PARSE_PARAMETERS_NONE();
    profiler_rshutdown();
    profiler_state_t *state = profiler_get_state();
    if (state && state->span_count > 0) {
        export_spans(state);
        state->span_count = 0;
    }
    RETURN_TRUE;
}

ZEND_NAMED_FUNCTION(zf_akari_get_span_count)
{
    ZEND_PARSE_PARAMETERS_NONE();
    profiler_state_t *state = profiler_get_state();
    RETURN_LONG(state ? (zend_long)state->span_count : 0);
}

ZEND_NAMED_FUNCTION(zf_akari_get_frame_count)
{
    ZEND_PARSE_PARAMETERS_NONE();
    profiler_state_t *state = profiler_get_state();
    RETURN_LONG(state ? (zend_long)state->frame_count : 0);
}

ZEND_NAMED_FUNCTION(zf_akari_get_spans_json)
{
    ZEND_PARSE_PARAMETERS_NONE();
    profiler_state_t *state = profiler_get_state();
    if (!state || state->span_count == 0) {
        RETURN_FALSE;
    }
    const char *service_name = AKARI_G(service_name);
    if (!service_name) service_name = "php";

    size_t json_len = 0;
    char *json = otlp_serialize_spans(state, service_name, &json_len);
    if (!json) {
        RETURN_FALSE;
    }

    zend_string *result = zend_string_init(json, json_len, 0);
    free(json);
    RETURN_STR(result);
}

/* ── Userland API: setTransactionName ── */

ZEND_NAMED_FUNCTION(zf_akari_set_transaction_name)
{
    zend_string *name;
    ZEND_PARSE_PARAMETERS_START(1, 1)
        Z_PARAM_STR(name)
    ZEND_PARSE_PARAMETERS_END();

    profiler_state_t *state = profiler_get_state();
    if (state) {
        size_t len = ZSTR_LEN(name);
        if (len >= ROOT_ATTR_MAX) len = ROOT_ATTR_MAX - 1;
        memcpy(state->custom_transaction_name, ZSTR_VAL(name), len);
        state->custom_transaction_name[len] = '\0';
        state->has_custom_transaction = 1;

        /* Also update root span name */
        if (state->root.active) {
            size_t name_len = len;
            if (name_len >= ROOT_ATTR_MAX) name_len = ROOT_ATTR_MAX - 1;
            memcpy(state->root.name, ZSTR_VAL(name), name_len);
            state->root.name[name_len] = '\0';
            state->root.name_len = name_len;
        }
    }
}

/* ── Userland API: getTransactionName ── */

ZEND_NAMED_FUNCTION(zf_akari_get_transaction_name)
{
    ZEND_PARSE_PARAMETERS_NONE();
    profiler_state_t *state = profiler_get_state();
    if (state && state->has_custom_transaction) {
        RETURN_STRING(state->custom_transaction_name);
    }
    if (state && state->root.active && state->root.name_len > 0) {
        RETURN_STRINGL(state->root.name, state->root.name_len);
    }
    RETURN_NULL();
}

/* ── Userland API: setServiceName ── */

ZEND_NAMED_FUNCTION(zf_akari_set_service_name)
{
    zend_string *name;
    ZEND_PARSE_PARAMETERS_START(1, 1)
        Z_PARAM_STR(name)
    ZEND_PARSE_PARAMETERS_END();

    /* Update module global service name */
    if (AKARI_G(service_name)) {
        free(AKARI_G(service_name));
    }
    AKARI_G(service_name) = strndup(ZSTR_VAL(name), ZSTR_LEN(name));
}

/* ── Userland API: addTag ── */

ZEND_NAMED_FUNCTION(zf_akari_add_tag)
{
    zend_string *key, *value;
    ZEND_PARSE_PARAMETERS_START(2, 2)
        Z_PARAM_STR(key)
        Z_PARAM_STR(value)
    ZEND_PARSE_PARAMETERS_END();

    profiler_state_t *state = profiler_get_state();
    if (state && state->tag_count < 16) {
        char buf[128];
        snprintf(buf, sizeof(buf), "%.*s=%.*s",
            (int)ZSTR_LEN(key), ZSTR_VAL(key),
            (int)ZSTR_LEN(value), ZSTR_VAL(value));
        /* Check for duplicates */
        for (int i = 0; i < state->tag_count; i++) {
            if (strncmp(state->tags[i], buf, ZSTR_LEN(key) + 1) == 0) {
                /* Replace existing tag */
                snprintf(state->tags[i], sizeof(state->tags[i]), "%s", buf);
                return;
            }
        }
        snprintf(state->tags[state->tag_count], sizeof(state->tags[state->tag_count]), "%s", buf);
        state->tag_count++;
    }
}

/* ── Userland API: getTags ── */

ZEND_NAMED_FUNCTION(zf_akari_get_tags)
{
    ZEND_PARSE_PARAMETERS_NONE();
    profiler_state_t *state = profiler_get_state();

    array_init(return_value);
    if (state) {
        for (int i = 0; i < state->tag_count; i++) {
            add_next_index_string(return_value, state->tags[i]);
        }
    }
}

/* ── Userland API: removeTag ── */

ZEND_NAMED_FUNCTION(zf_akari_remove_tag)
{
    zend_string *key;
    ZEND_PARSE_PARAMETERS_START(1, 1)
        Z_PARAM_STR(key)
    ZEND_PARSE_PARAMETERS_END();

    profiler_state_t *state = profiler_get_state();
    if (state) {
        size_t key_len = ZSTR_LEN(key);
        for (int i = 0; i < state->tag_count; i++) {
            if (strncmp(state->tags[i], ZSTR_VAL(key), key_len) == 0
                && state->tags[i][key_len] == '=') {
                /* Shift remaining tags down */
                for (int j = i; j < state->tag_count - 1; j++) {
                    memcpy(state->tags[j], state->tags[j + 1], sizeof(state->tags[j]));
                }
                state->tag_count--;
                break;
            }
        }
    }
}

/* ── Userland API: setCustomVariable ── */

ZEND_NAMED_FUNCTION(zf_akari_set_custom_variable)
{
    zend_string *key, *value;
    ZEND_PARSE_PARAMETERS_START(2, 2)
        Z_PARAM_STR(key)
        Z_PARAM_STR(value)
    ZEND_PARSE_PARAMETERS_END();

    /* Custom variables are stored as tags with a prefix */
    profiler_state_t *state = profiler_get_state();
    if (state && state->tag_count < 16) {
        char buf[128];
        snprintf(buf, sizeof(buf), "custom.%.*s=%.*s",
            (int)ZSTR_LEN(key), ZSTR_VAL(key),
            (int)ZSTR_LEN(value), ZSTR_VAL(value));
        snprintf(state->tags[state->tag_count], sizeof(state->tags[state->tag_count]), "%s", buf);
        state->tag_count++;
    }
}

/* ── Userland API: createSpan ──
 * Creates a manual span and returns its span_id (hex string).
 * The span is automatically finished at RSHUTDOWN.
 */

ZEND_NAMED_FUNCTION(zf_akari_create_span)
{
    zend_string *name;
    ZEND_PARSE_PARAMETERS_START(1, 1)
        Z_PARAM_STR(name)
    ZEND_PARSE_PARAMETERS_END();

    profiler_state_t *state = profiler_get_state();
    if (!state || !state->active) {
        RETURN_FALSE;
    }

    if (!ensure_span_capacity(state)) {
        RETURN_FALSE;
    }

    size_t span_idx = state->span_count++;
    profiler_span_t *span = &state->spans[span_idx];
    memset(span, 0, sizeof(profiler_span_t));

    memcpy(span->trace_id, state->trace_id, 32);
    profiler_generate_hex_id(state, span->span_id, 16);
    span->start_time_ns = realtime_ns();
    span->depth = state->stack_depth;
    span->kind = SPAN_KIND_INTERNAL;
    span->status_code = SPAN_STATUS_UNSET;

    /* Set name override */
    size_t name_len = ZSTR_LEN(name);
    if (name_len >= SPAN_NAME_OVERRIDE_MAX) name_len = SPAN_NAME_OVERRIDE_MAX - 1;
    memcpy(span->name_override, ZSTR_VAL(name), name_len);
    span->name_override[name_len] = '\0';
    span->name_override_len = name_len;

    /* Parent is root span or current top of stack */
    if (state->stack_depth > 0) {
        size_t parent_span_idx = state->stack[state->stack_depth - 1];
        memcpy(span->parent_span_id, state->spans[parent_span_idx].span_id, 16);
        span->has_parent = 1;
    } else if (state->root.active) {
        memcpy(span->parent_span_id, state->root.span_id, 16);
        span->has_parent = 1;
    }

    /* Track as manual span */
    if (state->manual_span_count < 32) {
        state->manual_spans[state->manual_span_count++] = (int)span_idx;
    }

    /* Return span_id as hex string */
    char span_id_hex[33];
    for (int i = 0; i < 16; i++) {
        snprintf(span_id_hex + i * 2, 3, "%02x", (unsigned char)span->span_id[i]);
    }
    span_id_hex[32] = '\0';

    RETURN_STRINGL(span_id_hex, 32);
}

/* ── Userland API: logException ── */

ZEND_NAMED_FUNCTION(zf_akari_log_exception)
{
    zval *exception;
    ZEND_PARSE_PARAMETERS_START(1, 1)
        Z_PARAM_OBJECT(exception)
    ZEND_PARSE_PARAMETERS_END();

    /* Verify it's a Throwable */
    if (!instanceof_function(Z_OBJCE_P(exception), zend_ce_throwable)) {
        zend_argument_error(NULL, 1, "must be an instance of Throwable, %s given", ZSTR_VAL(Z_OBJCE_P(exception)->name));
        RETURN_THROWS();
    }

    profiler_state_t *state = profiler_get_state();
    if (!state || !state->active) return;

    /* Record as exception event on the current span (if any) */
    uint32_t span_index = 0;
    if (state->stack_depth > 0) {
        span_index = (uint32_t)state->stack[state->stack_depth - 1];
    }

    if (state->exception_event_count >= state->exception_event_capacity) {
        size_t new_cap = state->exception_event_capacity
            ? state->exception_event_capacity * 2
            : PROFILER_INITIAL_EXCEPTION_EVENTS;
        profiler_exception_event_t *new_events = realloc(state->exception_events,
            new_cap * sizeof(profiler_exception_event_t));
        if (!new_events) return;
        state->exception_events = new_events;
        state->exception_event_capacity = new_cap;
    }

    profiler_exception_event_t *ev = &state->exception_events[state->exception_event_count];
    memset(ev, 0, sizeof(profiler_exception_event_t));
    ev->span_index = span_index;
    ev->timestamp_ns = realtime_ns();

    zend_class_entry *ce = Z_OBJCE_P(exception);
    if (ce && ce->name) {
        snprintf(ev->exception_type, EXCEPTION_TYPE_MAX, "%s", ZSTR_VAL(ce->name));
    }

    zval rv;
    zval *msg = zend_read_property_ex(ce, Z_OBJ_P(exception),
                    ZSTR_KNOWN(ZEND_STR_MESSAGE), 1, &rv);
    if (msg && Z_TYPE_P(msg) == IS_STRING) {
        size_t len = Z_STRLEN_P(msg);
        if (len >= EXCEPTION_MESSAGE_MAX) len = EXCEPTION_MESSAGE_MAX - 1;
        memcpy(ev->exception_message, Z_STRVAL_P(msg), len);
        ev->exception_message[len] = '\0';
    }

    state->exception_event_count++;

    /* Mark affected span as error */
    if (span_index > 0 && span_index < state->span_count) {
        state->spans[span_index].status_code = SPAN_STATUS_ERROR;
    }
}

/* ── Userland API: generateDistributedTracingHeaders ── */

ZEND_NAMED_FUNCTION(zf_akari_generate_distributed_headers)
{
    ZEND_PARSE_PARAMETERS_NONE();
    profiler_state_t *state = profiler_get_state();

    array_init(return_value);

    if (state) {
        /* Build traceparent: 00-{trace_id}-{parent_span_id}-01 */
        char traceparent[56]; /* "00-" + 32 + "-" + 16 + "-01" */
        char trace_id_hex[33];
        char span_id_hex[33];

        for (int i = 0; i < 16; i++) {
            snprintf(trace_id_hex + i * 2, 3, "%02x", (unsigned char)state->trace_id[i]);
            snprintf(span_id_hex + i * 2, 3, "%02x", (unsigned char)state->root.span_id[i]);
        }
        trace_id_hex[32] = '\0';
        span_id_hex[32] = '\0';

        snprintf(traceparent, sizeof(traceparent), "00-%s-%s-01", trace_id_hex, span_id_hex);
        add_assoc_string(return_value, "traceparent", traceparent);
    }
}

/* ── Userland API: markAsWebTransaction ── */

ZEND_NAMED_FUNCTION(zf_akari_mark_as_web)
{
    ZEND_PARSE_PARAMETERS_NONE();
    profiler_state_t *state = profiler_get_state();
    if (state) {
        state->root.is_cli = 0;
    }
}

/* ── Userland API: markAsCliTransaction ── */

ZEND_NAMED_FUNCTION(zf_akari_mark_as_cli)
{
    ZEND_PARSE_PARAMETERS_NONE();
    profiler_state_t *state = profiler_get_state();
    if (state) {
        state->root.is_cli = 1;
    }
}

/* ── Userland API: isProfiling ── */

ZEND_NAMED_FUNCTION(zf_akari_is_profiling)
{
    ZEND_PARSE_PARAMETERS_NONE();
    profiler_state_t *state = profiler_get_state();
    RETURN_BOOL(state && state->active);
}

static const zend_function_entry akari_functions[] = {
    ZEND_NS_RAW_FENTRY(PHP_AKARI_NS, "enable", zf_akari_enable, arginfo_enable, 0)
    ZEND_NS_RAW_FENTRY(PHP_AKARI_NS, "disable", zf_akari_disable, arginfo_disable, 0)
    ZEND_NS_RAW_FENTRY(PHP_AKARI_NS, "getSpanCount", zf_akari_get_span_count, arginfo_get_span_count, 0)
    ZEND_NS_RAW_FENTRY(PHP_AKARI_NS, "getFrameCount", zf_akari_get_frame_count, arginfo_get_frame_count, 0)
    ZEND_NS_RAW_FENTRY(PHP_AKARI_NS, "getSpansJson", zf_akari_get_spans_json, arginfo_get_spans_json, 0)
    /* Userland API */
    ZEND_NS_RAW_FENTRY(PHP_AKARI_NS, "setTransactionName", zf_akari_set_transaction_name, arginfo_set_transaction_name, 0)
    ZEND_NS_RAW_FENTRY(PHP_AKARI_NS, "getTransactionName", zf_akari_get_transaction_name, arginfo_get_transaction_name, 0)
    ZEND_NS_RAW_FENTRY(PHP_AKARI_NS, "setServiceName", zf_akari_set_service_name, arginfo_set_service_name, 0)
    ZEND_NS_RAW_FENTRY(PHP_AKARI_NS, "addTag", zf_akari_add_tag, arginfo_add_tag, 0)
    ZEND_NS_RAW_FENTRY(PHP_AKARI_NS, "getTags", zf_akari_get_tags, arginfo_get_tags, 0)
    ZEND_NS_RAW_FENTRY(PHP_AKARI_NS, "removeTag", zf_akari_remove_tag, arginfo_remove_tag, 0)
    ZEND_NS_RAW_FENTRY(PHP_AKARI_NS, "setCustomVariable", zf_akari_set_custom_variable, arginfo_set_custom_var, 0)
    ZEND_NS_RAW_FENTRY(PHP_AKARI_NS, "createSpan", zf_akari_create_span, arginfo_create_span, 0)
    ZEND_NS_RAW_FENTRY(PHP_AKARI_NS, "logException", zf_akari_log_exception, arginfo_log_exception, 0)
    ZEND_NS_RAW_FENTRY(PHP_AKARI_NS, "generateDistributedTracingHeaders", zf_akari_generate_distributed_headers, arginfo_distributed_headers, 0)
    ZEND_NS_RAW_FENTRY(PHP_AKARI_NS, "markAsWebTransaction", zf_akari_mark_as_web, arginfo_mark_as_web, 0)
    ZEND_NS_RAW_FENTRY(PHP_AKARI_NS, "markAsCliTransaction", zf_akari_mark_as_cli, arginfo_mark_as_cli, 0)
    ZEND_NS_RAW_FENTRY(PHP_AKARI_NS, "isProfiling", zf_akari_is_profiling, arginfo_is_profiling, 0)
    PHP_FE_END
};

PHP_MINIT_FUNCTION(akari)
{
    REGISTER_INI_ENTRIES();
    profiler_minit();
    udp_export_init(AKARI_G(udp_host), (int)AKARI_G(udp_port));
    engine_hooks_init(AKARI_G(trace_compile), AKARI_G(trace_gc));
    return SUCCESS;
}

PHP_MSHUTDOWN_FUNCTION(akari)
{
    engine_hooks_shutdown();
    udp_export_shutdown();
    UNREGISTER_INI_ENTRIES();
    profiler_mshutdown();
    return SUCCESS;
}

PHP_RINIT_FUNCTION(akari)
{
    /* Resolve PDO class entries once per request (safe time to call zend_lookup_class) */
    profiler_resolve_pdo_classes();

    if (AKARI_G(enable)) {
        int mode = get_tracer_mode();
        profiler_rinit(mode,
            (uint32_t)AKARI_G(max_depth),
            AKARI_G(trace_internal),
            AKARI_G(trace_functions),
            AKARI_G(sample_period),
            AKARI_G(min_duration_ms));
        profiler_set_flush_callback(flush_callback, NULL);
    }
    return SUCCESS;
}

PHP_RSHUTDOWN_FUNCTION(akari)
{
    if (AKARI_G(enable)) {
        profiler_rshutdown();
        profiler_state_t *state = profiler_get_state();
        if (state && state->span_count > 0) {
            export_spans(state);
            state->span_count = 0;
        }
    }
    return SUCCESS;
}

PHP_MINFO_FUNCTION(akari)
{
    php_info_print_table_start();
    php_info_print_table_header(2, "akari support", "enabled");
    php_info_print_table_row(2, "Version", PHP_AKARI_VERSION);
    php_info_print_table_row(2, "OTLP Protocol", "Yes (via UDP/msgpack forwarder)");
    php_info_print_table_end();

    php_info_print_table_start();
    php_info_print_table_header(2, "Hook Capabilities", "Status");
    php_info_print_table_row(2, "PDO / SQL", "enabled");
    php_info_print_table_row(2, "MySQLi", "enabled");
    php_info_print_table_row(2, "OCI8 / Oracle", "enabled");
    php_info_print_table_row(2, "Redis / RedisCluster", "enabled");
    php_info_print_table_row(2, "Predis", "enabled");
    php_info_print_table_row(2, "Memcached / Memcache", "enabled");
    php_info_print_table_row(2, "cURL", "enabled");
    php_info_print_table_row(2, "AMQP / php-amqplib", "enabled");
    php_info_print_table_row(2, "gRPC", "enabled");
    php_info_print_table_row(2, "rdkafka", "enabled");
    php_info_print_table_row(2, "SOAP", "enabled");
    php_info_print_table_row(2, "Elasticsearch", "enabled");
    php_info_print_table_row(2, "Pheanstalk", "enabled");
    php_info_print_table_row(2, "GraphQL", "enabled");
    php_info_print_table_row(2, "Twig", "enabled");
    php_info_print_table_row(2, "Doctrine ORM", "enabled");
    php_info_print_table_row(2, "Symfony", "enabled");
    php_info_print_table_row(2, "Laravel", "enabled");
    php_info_print_table_row(2, "Shopware 6", "enabled");
    php_info_print_table_row(2, "PHP Streams (HTTP)", "enabled");
    php_info_print_table_row(2, "PCRE / Regex", "enabled");
    php_info_print_table_row(2, "Filesystem Ops", "enabled");
    php_info_print_table_row(2, "Password Hashing", "enabled");
    php_info_print_table_row(2, "APCu", "enabled");
    php_info_print_table_row(2, "Session", "enabled");
    php_info_print_table_row(2, "DNS", "enabled");
    php_info_print_table_row(2, "Shell Execution", "enabled");
    php_info_print_table_row(2, "Mail", "enabled");
    php_info_print_table_row(2, "Sleep", "enabled");
    php_info_print_table_row(2, "Error Handling", "enabled");
    php_info_print_table_row(2, "Exception Hook (engine-level)", "enabled");
    php_info_print_table_row(2, "Compile File Profiling", AKARI_G(trace_compile) ? "enabled" : "disabled");
    php_info_print_table_row(2, "GC Monitoring", AKARI_G(trace_gc) ? "enabled" : "disabled");
    php_info_print_table_row(2, "Stack Trace Capture", "enabled");
    php_info_print_table_row(2, "SQL Normalization", "enabled");
    php_info_print_table_row(2, "W3C Trace Context", "enabled");
    php_info_print_table_row(2, "Sampling Mode", "enabled");
    php_info_print_table_row(2, "Userland API", "enabled");
    php_info_print_table_end();

    DISPLAY_INI_ENTRIES();
}

zend_module_entry akari_module_entry = {
    STANDARD_MODULE_HEADER,
    PHP_AKARI_EXTNAME,
    akari_functions,
    PHP_MINIT(akari),
    PHP_MSHUTDOWN(akari),
    PHP_RINIT(akari),
    PHP_RSHUTDOWN(akari),
    PHP_MINFO(akari),
    PHP_AKARI_VERSION,
    PHP_MODULE_GLOBALS(akari),
    PHP_GINIT(akari),
    NULL,
    NULL,
    STANDARD_MODULE_PROPERTIES_EX
};

#ifdef ZEND_COMPILE_DL_EXT
# ifdef ZTS
ZEND_TSRMLS_CACHE_DEFINE()
# endif
ZEND_GET_MODULE(akari)
#endif
