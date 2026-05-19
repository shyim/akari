#include "udp_export.h"
#include "msgpack_write.h"
#include "profiler_internal.h"
#include "../include/php_akari.h"

#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netdb.h>
#include <netinet/in.h>

/* ── UDP socket state ── */

static int g_udp_fd = -1;
static struct sockaddr_in g_udp_addr;

int udp_export_init(const char *host, int port)
{
    g_udp_fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (g_udp_fd < 0) return -1;

    struct addrinfo hints = {0}, *result;
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;

    char port_str[8];
    snprintf(port_str, sizeof(port_str), "%d", port);

    if (getaddrinfo(host, port_str, &hints, &result) != 0) {
        close(g_udp_fd);
        g_udp_fd = -1;
        return -1;
    }

    memcpy(&g_udp_addr, result->ai_addr, sizeof(g_udp_addr));
    freeaddrinfo(result);

    return 0;
}

void udp_export_shutdown(void)
{
    if (g_udp_fd >= 0) {
        close(g_udp_fd);
        g_udp_fd = -1;
    }
}

/* ── Span serialization ── */

static void write_span_msgpack(msgpack_buf_t *buf, profiler_state_t *state, const profiler_span_t *span, size_t span_idx)
{
    const profiler_frame_t *frame = profiler_get_frame(state, span->frame_index);
    const profiler_db_attr_t *db = profiler_get_db_attr(state, (uint32_t)span_idx);
    const profiler_http_attr_t *http = profiler_get_http_attr(state, (uint32_t)span_idx);
    const profiler_messaging_attr_t *msg = profiler_get_messaging_attr(state, (uint32_t)span_idx);
    const profiler_exception_event_t *exc = profiler_get_exception_event(state, (uint32_t)span_idx);

    uint32_t field_count = 12;
    if (db) field_count += 4;
    if (http) field_count += 5;
    if (msg) field_count += 3 + (msg->has_link ? 2 : 0);
    if (exc) field_count += 3; /* et (exception type), em (exception message), ev (event timestamp) */
    msgpack_write_map(buf, field_count);

    msgpack_write_key(buf, "s");
    msgpack_write_bin(buf, span->span_id, 16);
    msgpack_write_key(buf, "p");
    msgpack_write_bin(buf, span->parent_span_id, 16);
    msgpack_write_key(buf, "n");
    if (span->name_override_len > 0 && span->name_override_len < SPAN_NAME_OVERRIDE_MAX) {
        msgpack_write_str(buf, span->name_override, span->name_override_len);
    } else if (frame && frame->name_len > 0 && frame->name_len < PROFILER_NAME_MAX) {
        msgpack_write_str(buf, frame->name, frame->name_len);
    } else {
        msgpack_write_str(buf, "", 0);
    }
    msgpack_write_key(buf, "k");
    msgpack_write_uint8(buf, span->kind);
    msgpack_write_key(buf, "sc");
    msgpack_write_uint8(buf, span->status_code);
    msgpack_write_key(buf, "sm");
    msgpack_write_str(buf, "", 0);
    msgpack_write_key(buf, "ts");
    msgpack_write_uint64(buf, span->start_time_ns);
    msgpack_write_key(buf, "te");
    msgpack_write_uint64(buf, span->end_time_ns);
    msgpack_write_key(buf, "fn");
    if (frame && frame->function_len > 0) {
        msgpack_write_str(buf, frame->function, frame->function_len);
    } else {
        msgpack_write_str(buf, "", 0);
    }
    msgpack_write_key(buf, "fp");
    if (frame && frame->filepath_len > 0) {
        msgpack_write_str(buf, frame->filepath, frame->filepath_len);
    } else {
        msgpack_write_str(buf, "", 0);
    }
    msgpack_write_key(buf, "ns");
    if (frame && frame->ns_len > 0) {
        msgpack_write_str(buf, frame->ns, frame->ns_len);
    } else {
        msgpack_write_str(buf, "", 0);
    }
    msgpack_write_key(buf, "ln");
    msgpack_write_uint32(buf, frame ? frame->lineno : 0);

    if (db) {
        msgpack_write_key(buf, "ds");
        msgpack_write_str(buf, db->db_system, strlen(db->db_system));
        msgpack_write_key(buf, "dn");
        msgpack_write_str(buf, db->db_name, strlen(db->db_name));
        msgpack_write_key(buf, "du");
        msgpack_write_str(buf, db->db_user, strlen(db->db_user));
        msgpack_write_key(buf, "dq");
        msgpack_write_str(buf, db->db_statement, db->db_statement_len);
    }

    if (http) {
        msgpack_write_key(buf, "hu");
        msgpack_write_str(buf, http->url, http->url_len);
        msgpack_write_key(buf, "hm");
        msgpack_write_str(buf, http->method, strlen(http->method));
        msgpack_write_key(buf, "ha");
        msgpack_write_str(buf, http->server_address, strlen(http->server_address));
        msgpack_write_key(buf, "hp");
        msgpack_write_uint32(buf, http->server_port);
        msgpack_write_key(buf, "hc");
        msgpack_write_uint32(buf, (uint32_t)http->http_status_code);
    }

    if (msg) {
        msgpack_write_key(buf, "ms");
        msgpack_write_str(buf, msg->messaging_system, strlen(msg->messaging_system));
        msgpack_write_key(buf, "mo");
        msgpack_write_str(buf, msg->messaging_operation, strlen(msg->messaging_operation));
        msgpack_write_key(buf, "md");
        msgpack_write_str(buf, msg->destination_name, strlen(msg->destination_name));
        if (msg->has_link) {
            msgpack_write_key(buf, "lt");
            msgpack_write_bin(buf, msg->linked_trace_id, 32);
            msgpack_write_key(buf, "ls");
            msgpack_write_bin(buf, msg->linked_span_id, 16);
        }
    }

    if (exc) {
        msgpack_write_key(buf, "et");
        msgpack_write_str(buf, exc->exception_type, strlen(exc->exception_type));
        msgpack_write_key(buf, "em");
        msgpack_write_str(buf, exc->exception_message, strlen(exc->exception_message));
        msgpack_write_key(buf, "ev");
        msgpack_write_uint64(buf, exc->timestamp_ns);
    }
}

static void write_root_span_msgpack(msgpack_buf_t *buf, profiler_state_t *state)
{
    profiler_root_span_t *root = &state->root;

    uint32_t root_fields = 26;
    if (root->http_route[0]) root_fields++;
    if (root->http_controller[0]) root_fields++;
    msgpack_write_map(buf, root_fields);
    msgpack_write_key(buf, "s");
    msgpack_write_bin(buf, root->span_id, 16);
    msgpack_write_key(buf, "p");
    msgpack_write_bin(buf, root->parent_span_id, 16);
    msgpack_write_key(buf, "n");
    msgpack_write_str(buf, root->name, root->name_len);
    msgpack_write_key(buf, "k");
    msgpack_write_uint8(buf, SPAN_KIND_SERVER);
    msgpack_write_key(buf, "sc");
    msgpack_write_uint8(buf, root->status_code);
    msgpack_write_key(buf, "sm");
    msgpack_write_str(buf, root->status_message, strlen(root->status_message));
    msgpack_write_key(buf, "ts");
    msgpack_write_uint64(buf, root->start_time_ns);
    msgpack_write_key(buf, "te");
    msgpack_write_uint64(buf, root->end_time_ns);
    msgpack_write_key(buf, "fn");
    msgpack_write_str(buf, "", 0);
    msgpack_write_key(buf, "fp");
    msgpack_write_str(buf, "", 0);
    msgpack_write_key(buf, "ns");
    msgpack_write_str(buf, "", 0);
    msgpack_write_key(buf, "ln");
    msgpack_write_uint32(buf, 0);
    msgpack_write_key(buf, "hm");
    msgpack_write_str(buf, root->http_method, strlen(root->http_method));
    msgpack_write_key(buf, "up");
    msgpack_write_str(buf, root->url_path, strlen(root->url_path));
    msgpack_write_key(buf, "us");
    msgpack_write_str(buf, root->url_scheme, strlen(root->url_scheme));
    msgpack_write_key(buf, "sa");
    msgpack_write_str(buf, root->server_address, strlen(root->server_address));
    msgpack_write_key(buf, "pt");
    msgpack_write_uint32(buf, (uint32_t)root->server_port);
    msgpack_write_key(buf, "hc");
    msgpack_write_uint32(buf, (uint32_t)root->http_status_code);
    /* Runtime environment */
    msgpack_write_key(buf, "pv");
    msgpack_write_str(buf, root->php_version, strlen(root->php_version));
    msgpack_write_key(buf, "ps");
    msgpack_write_str(buf, root->php_sapi, strlen(root->php_sapi));
    msgpack_write_key(buf, "oc");
    msgpack_write_uint8(buf, (uint8_t)root->opcache_enabled);
    msgpack_write_key(buf, "om");
    msgpack_write_uint32(buf, (uint32_t)root->opcache_memory_mb);
    msgpack_write_key(buf, "mt");
    msgpack_write_uint32(buf, (uint32_t)root->max_execution_time);
    msgpack_write_key(buf, "ml");
    msgpack_write_uint32(buf, (uint32_t)root->memory_limit_mb);
    msgpack_write_key(buf, "pm");
    msgpack_write_uint64(buf, (uint64_t)root->peak_memory_bytes);
    msgpack_write_key(buf, "de");
    msgpack_write_uint8(buf, (uint8_t)root->display_errors);
    if (root->http_route[0]) {
        msgpack_write_key(buf, "hr");
        msgpack_write_str(buf, root->http_route, strlen(root->http_route));
    }
    if (root->http_controller[0]) {
        msgpack_write_key(buf, "hk");
        msgpack_write_str(buf, root->http_controller, strlen(root->http_controller));
    }
}

/* ── Export (synchronous sendto — ~1μs on localhost) ── */

#define UDP_MAX_DATAGRAM_SIZE 8000

static int send_datagram(const uint8_t *data, size_t len)
{
    if (g_udp_fd < 0 || len == 0 || len > UDP_MAX_DATAGRAM_SIZE) return 0;
    ssize_t sent = sendto(g_udp_fd, data, len, 0,
                          (struct sockaddr *)&g_udp_addr, sizeof(g_udp_addr));
    return (sent == (ssize_t)len);
}

static int span_is_exportable(profiler_state_t *state, size_t span_idx)
{
    /* Hold back spans whose exception fate is still undecided: exporting now
     * could emit a caught exception as an error, and an exported span cannot
     * be retracted. They become exportable once the pending event is resolved
     * (promoted or dropped), or at the final shutdown export. */
    if (profiler_span_has_pending_exception(state, (uint32_t)span_idx)) return 0;
    return !state->spans[span_idx].exported && state->spans[span_idx].end_time_ns > 0;
}

static void write_datagram_header(msgpack_buf_t *buf, profiler_state_t *state,
                                  const char *service_name, size_t total_spans)
{
    msgpack_write_map(buf, 4);
    msgpack_write_key(buf, "v");
    msgpack_write_uint8(buf, 1);
    msgpack_write_key(buf, "sn");
    msgpack_write_str(buf, service_name, strlen(service_name));
    msgpack_write_key(buf, "ti");
    msgpack_write_bin(buf, state->trace_id, 32);
    msgpack_write_key(buf, "sp");
    msgpack_write_array(buf, (uint32_t)total_spans);
}

static size_t encoded_header_size(profiler_state_t *state, const char *service_name,
                                  size_t total_spans)
{
    msgpack_buf_t buf;
    msgpack_buf_init(&buf, 128);
    write_datagram_header(&buf, state, service_name, total_spans);
    size_t len = buf.len;
    msgpack_buf_free(&buf);
    return len;
}

static size_t encoded_root_size(profiler_state_t *state)
{
    msgpack_buf_t buf;
    msgpack_buf_init(&buf, 1024);
    write_root_span_msgpack(&buf, state);
    size_t len = buf.len;
    msgpack_buf_free(&buf);
    return len;
}

static size_t encoded_span_size(profiler_state_t *state, size_t span_idx)
{
    msgpack_buf_t buf;
    msgpack_buf_init(&buf, 512);
    write_span_msgpack(&buf, state, &state->spans[span_idx], span_idx);
    size_t len = buf.len;
    msgpack_buf_free(&buf);
    return len;
}

static int send_span_batch(profiler_state_t *state, const char *service_name,
                           const size_t *span_indices, size_t span_count,
                           int include_root)
{
    if (span_count == 0 && !include_root) return 1;

    msgpack_buf_t buf;
    msgpack_buf_init(&buf, 8192);
    write_datagram_header(&buf, state, service_name, span_count + (include_root ? 1 : 0));

    if (include_root) {
        write_root_span_msgpack(&buf, state);
    }

    for (size_t i = 0; i < span_count; i++) {
        size_t span_idx = span_indices[i];
        write_span_msgpack(&buf, state, &state->spans[span_idx], span_idx);
    }

    int sent = send_datagram(buf.data, buf.len);
    if (sent) {
        for (size_t i = 0; i < span_count; i++) {
            state->spans[span_indices[i]].exported = 1;
        }
    }

    msgpack_buf_free(&buf);
    return sent;
}

void udp_export_spans(profiler_state_t *state, const char *service_name)
{
    if (g_udp_fd < 0 || !state) return;

    int include_root = (state->root.end_time_ns > 0 && !state->root.is_cli);

    size_t new_spans = 0;
    for (size_t i = 0; i < state->span_count; i++) {
        if (span_is_exportable(state, i)) {
            new_spans++;
        }
    }
    if (new_spans == 0 && !include_root) return;

    size_t *batch_indices = NULL;
    if (new_spans > 0) {
        batch_indices = malloc(new_spans * sizeof(size_t));
        if (!batch_indices) return;
    }

    size_t batch_count = 0;
    size_t batch_payload_size = 0;
    size_t root_size = include_root ? encoded_root_size(state) : 0;

    for (size_t i = 0; i < state->span_count; i++) {
        if (!span_is_exportable(state, i)) continue;

        size_t span_size = encoded_span_size(state, i);
        while (1) {
            size_t total_spans = batch_count + 1 + (include_root ? 1 : 0);
            size_t candidate_size = encoded_header_size(state, service_name, total_spans)
                + (include_root ? root_size : 0)
                + batch_payload_size
                + span_size;

            if (candidate_size <= UDP_MAX_DATAGRAM_SIZE) {
                batch_indices[batch_count++] = i;
                batch_payload_size += span_size;
                break;
            }

            if (batch_count > 0 || include_root) {
                send_span_batch(state, service_name, batch_indices, batch_count, include_root);
                batch_count = 0;
                batch_payload_size = 0;
                include_root = 0;
                continue;
            }

            /* A single span is too large for UDP; leave it unexported. */
            break;
        }
    }

    if (batch_count > 0 || include_root) {
        send_span_batch(state, service_name, batch_indices, batch_count, include_root);
    }

    free(batch_indices);
}
