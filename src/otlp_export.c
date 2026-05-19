/*
 * OTLP JSON serialization — used by Akari\getSpansJson() for introspection/testing.
 * The actual export path uses UDP+msgpack via udp_export.c.
 */

#include "otlp_export.h"
#include "profiler_internal.h"
#include "../include/php_akari.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>

/* ── JSON buffer ── */

typedef struct {
    char *buf;
    size_t len;
    size_t cap;
} json_buf_t;

static void jb_init(json_buf_t *jb, size_t initial_cap)
{
    jb->buf = malloc(initial_cap);
    jb->len = 0;
    jb->cap = jb->buf ? initial_cap : 0;
}

static int jb_ensure(json_buf_t *jb, size_t additional)
{
    if (jb->len + additional <= jb->cap) return 1;
    size_t new_cap = jb->cap * 2;
    if (new_cap < jb->len + additional) new_cap = jb->len + additional + 4096;
    char *new_buf = realloc(jb->buf, new_cap);
    if (!new_buf) return 0;
    jb->buf = new_buf;
    jb->cap = new_cap;
    return 1;
}

static void jb_append(json_buf_t *jb, const char *s, size_t len)
{
    if (!jb_ensure(jb, len)) return;
    memcpy(jb->buf + jb->len, s, len);
    jb->len += len;
}

static void jb_str(json_buf_t *jb, const char *s) { jb_append(jb, s, strlen(s)); }
static void jb_char(json_buf_t *jb, char c) { jb_append(jb, &c, 1); }

static void jb_uint64(json_buf_t *jb, uint64_t v)
{
    char tmp[24];
    int n = snprintf(tmp, sizeof(tmp), "%llu", (unsigned long long)v);
    jb_append(jb, tmp, profiler_clamp_snprintf_len(n, sizeof(tmp)));
}

static void jb_json_escaped(json_buf_t *jb, const char *s, size_t len)
{
    for (size_t i = 0; i < len; i++) {
        char c = s[i];
        switch (c) {
            case '"': jb_str(jb, "\\\""); break;
            case '\\': jb_str(jb, "\\\\"); break;
            case '\n': jb_str(jb, "\\n"); break;
            case '\r': jb_str(jb, "\\r"); break;
            case '\t': jb_str(jb, "\\t"); break;
            default:
                if ((unsigned char)c < 0x20) {
                    char esc[7];
                    snprintf(esc, sizeof(esc), "\\u%04x", (unsigned char)c);
                    jb_append(jb, esc, 6);
                } else {
                    jb_char(jb, c);
                }
        }
    }
}

/* ── Span JSON ── */

static void write_span_json(json_buf_t *jb, profiler_state_t *state, const profiler_span_t *span)
{
    const profiler_frame_t *frame = profiler_get_frame(state, span->frame_index);

    jb_str(jb, "{\"traceId\":\"");
    jb_append(jb, span->trace_id, 32);
    jb_str(jb, "\",\"spanId\":\"");
    jb_append(jb, span->span_id, 16);
    jb_char(jb, '"');

    if (span->has_parent) {
        jb_str(jb, ",\"parentSpanId\":\"");
        jb_append(jb, span->parent_span_id, 16);
        jb_char(jb, '"');
    }

    jb_str(jb, ",\"name\":\"");
    if (span->name_override_len > 0 && span->name_override_len < SPAN_NAME_OVERRIDE_MAX) {
        jb_json_escaped(jb, span->name_override, span->name_override_len);
    } else if (frame && frame->name_len > 0 && frame->name_len < PROFILER_NAME_MAX) {
        jb_json_escaped(jb, frame->name, frame->name_len);
    }

    jb_str(jb, "\",\"kind\":");
    jb_uint64(jb, span->kind);
    jb_str(jb, ",\"startTimeUnixNano\":\"");
    jb_uint64(jb, span->start_time_ns);
    jb_str(jb, "\",\"endTimeUnixNano\":\"");
    jb_uint64(jb, span->end_time_ns);

    jb_str(jb, "\",\"attributes\":[");
    int attr_count = 0;

    if (frame && frame->function_len > 0) {
        jb_str(jb, "{\"key\":\"code.function\",\"value\":{\"stringValue\":\"");
        jb_json_escaped(jb, frame->function, frame->function_len);
        jb_str(jb, "\"}}");
        attr_count++;
    }
    if (frame && frame->ns_len > 0) {
        if (attr_count > 0) jb_char(jb, ',');
        jb_str(jb, "{\"key\":\"code.namespace\",\"value\":{\"stringValue\":\"");
        jb_json_escaped(jb, frame->ns, frame->ns_len);
        jb_str(jb, "\"}}");
        attr_count++;
    }
    if (frame && frame->filepath_len > 0) {
        if (attr_count > 0) jb_char(jb, ',');
        jb_str(jb, "{\"key\":\"code.filepath\",\"value\":{\"stringValue\":\"");
        jb_json_escaped(jb, frame->filepath, frame->filepath_len);
        jb_str(jb, "\"}}");
        attr_count++;
    }
    if (frame && frame->lineno > 0) {
        if (attr_count > 0) jb_char(jb, ',');
        jb_str(jb, "{\"key\":\"code.lineno\",\"value\":{\"intValue\":\"");
        jb_uint64(jb, frame->lineno);
        jb_str(jb, "\"}}");
        attr_count++;
    }
    if (attr_count > 0) jb_char(jb, ',');
    jb_str(jb, "{\"key\":\"code.stacktrace.depth\",\"value\":{\"intValue\":\"");
    jb_uint64(jb, span->depth);
    jb_str(jb, "\"}}");
    attr_count++;

    /* Database attributes (PDO spans) */
    uint32_t span_idx = (uint32_t)(span - state->spans);
    const profiler_db_attr_t *db = profiler_get_db_attr(state, span_idx);
    if (db) {
        if (db->db_system[0]) {
            jb_char(jb, ',');
            jb_str(jb, "{\"key\":\"db.system\",\"value\":{\"stringValue\":\"");
            jb_json_escaped(jb, db->db_system, strlen(db->db_system));
            jb_str(jb, "\"}}");
        }
        if (db->db_name[0]) {
            jb_char(jb, ',');
            jb_str(jb, "{\"key\":\"db.name\",\"value\":{\"stringValue\":\"");
            jb_json_escaped(jb, db->db_name, strlen(db->db_name));
            jb_str(jb, "\"}}");
        }
        if (db->db_user[0]) {
            jb_char(jb, ',');
            jb_str(jb, "{\"key\":\"db.user\",\"value\":{\"stringValue\":\"");
            jb_json_escaped(jb, db->db_user, strlen(db->db_user));
            jb_str(jb, "\"}}");
        }
        if (db->db_statement_len > 0) {
            jb_char(jb, ',');
            jb_str(jb, "{\"key\":\"db.statement\",\"value\":{\"stringValue\":\"");
            jb_json_escaped(jb, db->db_statement, db->db_statement_len);
            jb_str(jb, "\"}}");
        }
    }

    /* HTTP client attributes (curl spans) */
    const profiler_http_attr_t *http = profiler_get_http_attr(state, span_idx);
    if (http) {
        if (http->method[0]) {
            jb_char(jb, ',');
            jb_str(jb, "{\"key\":\"http.request.method\",\"value\":{\"stringValue\":\"");
            jb_json_escaped(jb, http->method, strlen(http->method));
            jb_str(jb, "\"}}");
        }
        if (http->url_len > 0) {
            jb_char(jb, ',');
            jb_str(jb, "{\"key\":\"url.full\",\"value\":{\"stringValue\":\"");
            jb_json_escaped(jb, http->url, http->url_len);
            jb_str(jb, "\"}}");
        }
        if (http->server_address[0]) {
            jb_char(jb, ',');
            jb_str(jb, "{\"key\":\"server.address\",\"value\":{\"stringValue\":\"");
            jb_json_escaped(jb, http->server_address, strlen(http->server_address));
            jb_str(jb, "\"}}");
        }
        if (http->server_port > 0) {
            jb_char(jb, ',');
            jb_str(jb, "{\"key\":\"server.port\",\"value\":{\"intValue\":\"");
            jb_uint64(jb, http->server_port);
            jb_str(jb, "\"}}");
        }
        if (http->http_status_code > 0) {
            jb_char(jb, ',');
            jb_str(jb, "{\"key\":\"http.response.status_code\",\"value\":{\"intValue\":\"");
            jb_uint64(jb, http->http_status_code);
            jb_str(jb, "\"}}");
        }
    }

    /* Messaging attributes (AMQP spans) */
    const profiler_messaging_attr_t *msg = profiler_get_messaging_attr(state, span_idx);
    if (msg) {
        if (msg->messaging_system[0]) {
            jb_char(jb, ',');
            jb_str(jb, "{\"key\":\"messaging.system\",\"value\":{\"stringValue\":\"");
            jb_json_escaped(jb, msg->messaging_system, strlen(msg->messaging_system));
            jb_str(jb, "\"}}");
        }
        if (msg->messaging_operation[0]) {
            jb_char(jb, ',');
            jb_str(jb, "{\"key\":\"messaging.operation.name\",\"value\":{\"stringValue\":\"");
            jb_json_escaped(jb, msg->messaging_operation, strlen(msg->messaging_operation));
            jb_str(jb, "\"}}");
        }
        if (msg->destination_name[0]) {
            jb_char(jb, ',');
            jb_str(jb, "{\"key\":\"messaging.destination.name\",\"value\":{\"stringValue\":\"");
            jb_json_escaped(jb, msg->destination_name, strlen(msg->destination_name));
            jb_str(jb, "\"}}");
        }
    }

    jb_str(jb, "]");

    /* Span links (for consumer spans linked to producer traces) */
    if (msg && msg->has_link) {
        jb_str(jb, ",\"links\":[{\"traceId\":\"");
        jb_append(jb, msg->linked_trace_id, 32);
        jb_str(jb, "\",\"spanId\":\"");
        jb_append(jb, msg->linked_span_id, 16);
        jb_str(jb, "\"}]");
    }

    /* Exception events */
    const profiler_exception_event_t *exc = profiler_get_exception_event(state, span_idx);
    if (exc) {
        jb_str(jb, ",\"events\":[{\"name\":\"exception\",\"timeUnixNano\":\"");
        jb_uint64(jb, exc->timestamp_ns);
        jb_str(jb, "\",\"attributes\":[");
        if (exc->exception_type[0]) {
            jb_str(jb, "{\"key\":\"exception.type\",\"value\":{\"stringValue\":\"");
            jb_json_escaped(jb, exc->exception_type, strlen(exc->exception_type));
            jb_str(jb, "\"}}");
        }
        if (exc->exception_message[0]) {
            if (exc->exception_type[0]) jb_char(jb, ',');
            jb_str(jb, "{\"key\":\"exception.message\",\"value\":{\"stringValue\":\"");
            jb_json_escaped(jb, exc->exception_message, strlen(exc->exception_message));
            jb_str(jb, "\"}}");
        }
        jb_str(jb, "]}]");
    }

    jb_str(jb, ",\"status\":{\"code\":");
    jb_uint64(jb, span->status_code);
    jb_str(jb, "}}");
}

static void write_root_span_json(json_buf_t *jb, profiler_state_t *state)
{
    profiler_root_span_t *root = &state->root;

    jb_str(jb, "{\"traceId\":\"");
    jb_append(jb, state->trace_id, 32);
    jb_str(jb, "\",\"spanId\":\"");
    jb_append(jb, root->span_id, 16);
    jb_char(jb, '"');

    if (root->has_parent) {
        jb_str(jb, ",\"parentSpanId\":\"");
        jb_append(jb, root->parent_span_id, 16);
        jb_char(jb, '"');
    }

    jb_str(jb, ",\"name\":\"");
    jb_json_escaped(jb, root->name, root->name_len);
    jb_str(jb, "\",\"kind\":");
    jb_uint64(jb, SPAN_KIND_SERVER);
    jb_str(jb, ",\"startTimeUnixNano\":\"");
    jb_uint64(jb, root->start_time_ns);
    jb_str(jb, "\",\"endTimeUnixNano\":\"");
    jb_uint64(jb, root->end_time_ns);

    jb_str(jb, "\",\"attributes\":[");
    int n = 0;
    if (root->http_method[0]) {
        jb_str(jb, "{\"key\":\"http.request.method\",\"value\":{\"stringValue\":\"");
        jb_json_escaped(jb, root->http_method, strlen(root->http_method));
        jb_str(jb, "\"}}"); n++;
    }
    if (root->url_path[0]) {
        if (n) jb_char(jb, ',');
        jb_str(jb, "{\"key\":\"url.path\",\"value\":{\"stringValue\":\"");
        jb_json_escaped(jb, root->url_path, strlen(root->url_path));
        jb_str(jb, "\"}}"); n++;
    }
    if (root->http_status_code > 0) {
        if (n) jb_char(jb, ',');
        jb_str(jb, "{\"key\":\"http.response.status_code\",\"value\":{\"intValue\":\"");
        jb_uint64(jb, root->http_status_code);
        jb_str(jb, "\"}}"); n++;
    }
    if (root->http_route[0]) {
        if (n) jb_char(jb, ',');
        jb_str(jb, "{\"key\":\"http.route\",\"value\":{\"stringValue\":\"");
        jb_json_escaped(jb, root->http_route, strlen(root->http_route));
        jb_str(jb, "\"}}"); n++;
    }
    if (root->http_controller[0]) {
        if (n) jb_char(jb, ',');
        jb_str(jb, "{\"key\":\"http.controller\",\"value\":{\"stringValue\":\"");
        jb_json_escaped(jb, root->http_controller, strlen(root->http_controller));
        jb_str(jb, "\"}}"); n++;
    }

    /* Runtime environment annotations */
    if (root->php_version[0]) {
        if (n) jb_char(jb, ',');
        jb_str(jb, "{\"key\":\"php.version\",\"value\":{\"stringValue\":\"");
        jb_json_escaped(jb, root->php_version, strlen(root->php_version));
        jb_str(jb, "\"}}"); n++;
    }
    if (root->php_sapi[0]) {
        if (n) jb_char(jb, ',');
        jb_str(jb, "{\"key\":\"php.sapi\",\"value\":{\"stringValue\":\"");
        jb_json_escaped(jb, root->php_sapi, strlen(root->php_sapi));
        jb_str(jb, "\"}}"); n++;
    }
    {
        if (n) jb_char(jb, ',');
        jb_str(jb, "{\"key\":\"php.opcache.enabled\",\"value\":{\"intValue\":\"");
        jb_uint64(jb, root->opcache_enabled);
        jb_str(jb, "\"}}"); n++;
    }
    if (root->opcache_enabled) {
        if (n) jb_char(jb, ',');
        jb_str(jb, "{\"key\":\"php.opcache.memory_consumption_mb\",\"value\":{\"intValue\":\"");
        jb_uint64(jb, root->opcache_memory_mb);
        jb_str(jb, "\"}}"); n++;
        if (n) jb_char(jb, ',');
        jb_str(jb, "{\"key\":\"php.opcache.max_accelerated_files\",\"value\":{\"intValue\":\"");
        jb_uint64(jb, root->opcache_max_files);
        jb_str(jb, "\"}}"); n++;
        if (n) jb_char(jb, ',');
        jb_str(jb, "{\"key\":\"php.opcache.interned_strings_buffer_mb\",\"value\":{\"intValue\":\"");
        jb_uint64(jb, root->opcache_interned_mb);
        jb_str(jb, "\"}}"); n++;
    }
    if (root->date_timezone[0]) {
        if (n) jb_char(jb, ',');
        jb_str(jb, "{\"key\":\"php.date.timezone\",\"value\":{\"stringValue\":\"");
        jb_json_escaped(jb, root->date_timezone, strlen(root->date_timezone));
        jb_str(jb, "\"}}"); n++;
    }
    {
        if (n) jb_char(jb, ',');
        jb_str(jb, "{\"key\":\"php.max_execution_time\",\"value\":{\"intValue\":\"");
        jb_uint64(jb, root->max_execution_time);
        jb_str(jb, "\"}}"); n++;
    }
    if (root->memory_limit_mb > 0) {
        if (n) jb_char(jb, ',');
        jb_str(jb, "{\"key\":\"php.memory_limit_mb\",\"value\":{\"intValue\":\"");
        jb_uint64(jb, root->memory_limit_mb);
        jb_str(jb, "\"}}"); n++;
    }
    if (root->peak_memory_bytes > 0) {
        if (n) jb_char(jb, ',');
        jb_str(jb, "{\"key\":\"php.memory.peak_usage_bytes\",\"value\":{\"intValue\":\"");
        jb_uint64(jb, root->peak_memory_bytes);
        jb_str(jb, "\"}}"); n++;
    }
    {
        if (n) jb_char(jb, ',');
        jb_str(jb, "{\"key\":\"php.display_errors\",\"value\":{\"intValue\":\"");
        jb_uint64(jb, root->display_errors);
        jb_str(jb, "\"}}"); n++;
    }

    jb_str(jb, "],\"status\":{\"code\":");
    jb_uint64(jb, root->status_code);
    if (root->status_message[0]) {
        jb_str(jb, ",\"message\":\"");
        jb_json_escaped(jb, root->status_message, strlen(root->status_message));
        jb_char(jb, '"');
    }
    jb_str(jb, "}}");
}

/* ── Serialization (for getSpansJson introspection) ── */

char *otlp_serialize_spans(profiler_state_t *state, const char *service_name, size_t *out_len)
{
    int has_root = state && state->root.active == 0 && state->root.end_time_ns > 0;
    if (!state || (state->span_count == 0 && !has_root)) {
        if (out_len) *out_len = 0;
        return NULL;
    }

    json_buf_t jb;
    jb_init(&jb, state->span_count * 300 + 2048);
    if (!jb.buf) {
        if (out_len) *out_len = 0;
        return NULL;
    }

    jb_str(&jb, "{\"resourceSpans\":[{\"resource\":{\"attributes\":["
                 "{\"key\":\"service.name\",\"value\":{\"stringValue\":\"");
    jb_json_escaped(&jb, service_name, strlen(service_name));
    jb_str(&jb, "\"}}]},\"scopeSpans\":[{\"scope\":{\"name\":\"akari\","
                 "\"version\":\"" PHP_AKARI_VERSION "\"},\"spans\":[");

    int first = 1;
    if (has_root) {
        write_root_span_json(&jb, state);
        first = 0;
    }
    for (size_t i = 0; i < state->span_count; i++) {
        if (!first) jb_char(&jb, ',');
        first = 0;
        write_span_json(&jb, state, &state->spans[i]);
    }

    jb_str(&jb, "]}]}]}");

    if (out_len) *out_len = jb.len;
    return jb.buf;
}
