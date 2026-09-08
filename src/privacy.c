#include "profiler_internal.h"

/* Strip all query/fragment data and userinfo, including credentials embedded
 * in malformed authorities. This is deliberately independent of key names. */
void profiler_copy_url(char *dst, size_t cap, const char *url, size_t len)
{
    if (cap == 0) return;
    size_t end = 0, authority = 0;
    while (end < len && url[end] && url[end] != '?' && url[end] != '#') end++;
    for (size_t i = 0; i+2 < end; i++) {
        if (url[i] == ':' && url[i+1] == '/' && url[i+2] == '/') { authority = i+3; break; }
    }
    if (authority == 0 && end >= 2 && url[0] == '/' && url[1] == '/') authority = 2;
    size_t start = authority;
    if (authority) {
        for (size_t i = authority; i < end && url[i] != '/'; i++) if (url[i] == '@') start = i+1;
    }
    /* Locate userinfo in the ORIGINAL input, before truncation can remove @. */
    size_t prefix = authority < cap-1 ? authority : cap-1;
    size_t tail = end-start;
    if (tail > cap-1-prefix) tail = cap-1-prefix;
    memmove(dst, url, prefix);
    memmove(dst+prefix, url+start, tail);
    dst[prefix+tail] = '\0';
}

void profiler_sanitize_url(char *url)
{
    size_t len = strlen(url);
    profiler_copy_url(url, len+1, url, len);
}

void profiler_set_http_url(profiler_http_attr_t *attr, const char *url, size_t len)
{
    profiler_copy_url(attr->url, sizeof(attr->url), url, len);
    attr->url_len = strlen(attr->url);
    const char *authority = strstr(attr->url, "://");
    if (!authority) return;
    authority += 3;
    const char *end = authority;
    while (*end && *end != '/') end++;
    const char *host_end = end, *port = NULL;
    if (authority < end && *authority == '[') {
        const char *bracket = memchr(authority, ']', (size_t)(end-authority));
        if (!bracket) return;
        host_end = bracket+1;
        if (host_end < end && *host_end == ':') port = host_end+1;
    } else {
        const char *colon = memchr(authority, ':', (size_t)(end-authority));
        if (colon) { host_end = colon; port = colon+1; }
    }
    size_t host_len = (size_t)(host_end-authority);
    if (host_len >= sizeof(attr->server_address)) host_len = sizeof(attr->server_address)-1;
    memcpy(attr->server_address, authority, host_len);
    attr->server_address[host_len] = '\0';
    unsigned int port_number = 0;
    if (port) {
        for (const char *p = port; p < end; p++) {
            if (*p < '0' || *p > '9') return;
            port_number = port_number*10+(unsigned int)(*p-'0');
            if (port_number > 65535) return;
        }
        attr->server_port = (uint16_t)port_number;
    }
}

/* All serialization paths share this boundary. Raw arbitrary text cannot be
 * reliably scrubbed with keyword matching, so it is omitted by default.
 * capture_sensitive is SYSTEM-only and explicitly opts into raw application
 * text. URL credentials/query strings are removed even with that opt-in. */
void profiler_sanitize_for_export(profiler_state_t *state)
{
    if (!state) return;
    profiler_root_span_t *root = &state->root;
    profiler_sanitize_url(root->url_path);
    for (size_t i = 0; i < state->http_attr_count; i++) {
        profiler_sanitize_url(state->http_attrs[i].url);
        state->http_attrs[i].url_len = strlen(state->http_attrs[i].url);
    }
    if (AKARI_G(capture_sensitive)) return;

    root->process_command_line[0] = '\0';
    root->status_message[0] = '\0';
    state->root_exception_message[0] = '\0';
    if (!root->is_cli && root->name_is_auto) {
        int n = snprintf(root->name, sizeof(root->name), "%s", root->http_method[0] ? root->http_method : "HTTP");
        root->name_len = profiler_clamp_snprintf_len(n, sizeof(root->name));
    }
    for (size_t i = 0; i < state->frame_count; i++) {
        state->frames[i].filepath[0] = '\0';
        state->frames[i].filepath_len = 0;
    }
    for (size_t i = 0; i < state->db_attr_count; i++) {
        profiler_db_attr_t *db = &state->db_attrs[i];
        db->db_statement[0] = '\0';
        db->db_statement_len = 0;
        db->db_user[0] = '\0';
        db->db_name[0] = '\0';
    }
    for (size_t i = 0; i < state->exception_event_count; i++) {
        state->exception_events[i].exception_message[0] = '\0';
    }
    for (size_t i = 0; i < state->log_record_count; i++) {
        profiler_log_record_t *rec = &state->log_records[i];
        memcpy(rec->body, "[redacted]", sizeof("[redacted]"));
        rec->body_len = sizeof("[redacted]")-1;
        for (int j = 0; j < rec->attr_count; j++) {
            snprintf(rec->attrs[j].value, sizeof(rec->attrs[j].value), "[redacted]");
        }
    }
    for (int i = 0; i < state->tag_count; i++) {
        char *value = strchr(state->tags[i], '=');
        if (value) {
            size_t remaining = sizeof(state->tags[i]) - (size_t)(value+1-state->tags[i]);
            snprintf(value+1, remaining, "[redacted]");
        }
    }
}
