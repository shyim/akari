#include "profiler_internal.h"

/* ── Root span + traceparent ── */

static int is_valid_hex(const char *s, size_t len)
{
    for (size_t i = 0; i < len; i++) {
        char c = s[i];
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F')))
            return 0;
    }
    return 1;
}

/**
 * Parse W3C traceparent header: "00-{32hex}-{16hex}-{2hex}"
 * Returns 1 if valid, populates trace_id and parent_id.
 */
static int parse_traceparent(const char *header, char *trace_id_out, char *parent_id_out)
{
    if (!header || strlen(header) < 55) return 0;

    /* version: 2 chars */
    if (header[2] != '-') return 0;
    /* trace_id: 32 chars */
    if (!is_valid_hex(header + 3, 32)) return 0;
    if (header[35] != '-') return 0;
    /* parent_id: 16 chars */
    if (!is_valid_hex(header + 36, 16)) return 0;
    if (header[52] != '-') return 0;

    memcpy(trace_id_out, header + 3, 32);
    memcpy(parent_id_out, header + 36, 16);
    return 1;
}

void init_root_span(profiler_state_t *state)
{
    profiler_root_span_t *root = &state->root;
    memset(root, 0, sizeof(profiler_root_span_t));

    /* Detect SAPI type */
    root->is_cli = (strcmp(sapi_module.name, "cli") == 0);

    if (root->is_cli) {
        root->active = 0;
        return;
    }

    /* Generate root span ID */
    profiler_generate_hex_id(state, root->span_id, 16);
    root->start_time_ns = realtime_ns();
    root->status_code = SPAN_STATUS_UNSET;
    root->active = 1;

    /* HTTP attributes from SAPI */
    if (SG(request_info).request_method) {
        snprintf(root->http_method, sizeof(root->http_method), "%s",
                 SG(request_info).request_method);
    }
    if (SG(request_info).request_uri) {
        snprintf(root->url_path, sizeof(root->url_path), "%s",
                 SG(request_info).request_uri);
    }

    /* URL scheme */
    const char *https = sapi_getenv("HTTPS", 5);
    if (https && strcmp(https, "on") == 0) {
        strcpy(root->url_scheme, "https");
    } else {
        strcpy(root->url_scheme, "http");
    }

    /* Server address/port */
    const char *host = sapi_getenv("HTTP_HOST", 9);
    if (host) {
        snprintf(root->server_address, sizeof(root->server_address), "%s", host);
    } else {
        const char *server_name = sapi_getenv("SERVER_NAME", 11);
        if (server_name) {
            snprintf(root->server_address, sizeof(root->server_address), "%s", server_name);
        }
    }
    const char *port_str = sapi_getenv("SERVER_PORT", 11);
    if (port_str) {
        root->server_port = atoi(port_str);
    }

    /* Span name: "METHOD /path" */
    if (root->http_method[0] && root->url_path[0]) {
        root->name_len = snprintf(root->name, sizeof(root->name), "%s %s",
                                   root->http_method, root->url_path);
    } else if (root->http_method[0]) {
        root->name_len = snprintf(root->name, sizeof(root->name), "%s", root->http_method);
    } else {
        root->name_len = snprintf(root->name, sizeof(root->name), "HTTP");
    }

    /* Try to read traceparent header */
    const char *traceparent = sapi_getenv("HTTP_TRACEPARENT", 16);
    if (traceparent && parse_traceparent(traceparent, state->trace_id, root->parent_span_id)) {
        root->has_parent = 1;
        /* trace_id is now from the incoming traceparent */
    }

    /* Runtime environment annotations */
    snprintf(root->php_version, sizeof(root->php_version), "%s", PHP_VERSION);
    snprintf(root->php_sapi, sizeof(root->php_sapi), "%s", sapi_module.name);

    /* OPcache settings (if extension is loaded) */
    const char *val;
    val = zend_ini_string_ex("opcache.enable", sizeof("opcache.enable") - 1, 0, NULL);
    root->opcache_enabled = (val && strcmp(val, "1") == 0) ? 1 : 0;

    val = zend_ini_string_ex("opcache.memory_consumption", sizeof("opcache.memory_consumption") - 1, 0, NULL);
    root->opcache_memory_mb = val ? atol(val) : 0;

    val = zend_ini_string_ex("opcache.max_accelerated_files", sizeof("opcache.max_accelerated_files") - 1, 0, NULL);
    root->opcache_max_files = val ? atol(val) : 0;

    val = zend_ini_string_ex("opcache.interned_strings_buffer", sizeof("opcache.interned_strings_buffer") - 1, 0, NULL);
    root->opcache_interned_mb = val ? atol(val) : 0;

    /* PHP runtime settings */
    val = zend_ini_string_ex("date.timezone", sizeof("date.timezone") - 1, 0, NULL);
    if (val && val[0]) {
        snprintf(root->date_timezone, sizeof(root->date_timezone), "%s", val);
    }

    val = zend_ini_string_ex("max_execution_time", sizeof("max_execution_time") - 1, 0, NULL);
    root->max_execution_time = val ? atol(val) : 0;

    val = zend_ini_string_ex("memory_limit", sizeof("memory_limit") - 1, 0, NULL);
    if (val) {
        long ml = atol(val);
        size_t len = strlen(val);
        if (len > 0) {
            char suffix = val[len - 1];
            if (suffix == 'M' || suffix == 'm') ml *= 1;
            else if (suffix == 'G' || suffix == 'g') ml *= 1024;
            else ml = ml / (1024 * 1024); /* bytes to MB */
        }
        root->memory_limit_mb = ml;
    }

    val = zend_ini_string_ex("realpath_cache_size", sizeof("realpath_cache_size") - 1, 0, NULL);
    root->realpath_cache_size = val ? atol(val) : 0;

    val = zend_ini_string_ex("display_errors", sizeof("display_errors") - 1, 0, NULL);
    root->display_errors = (val && (strcmp(val, "1") == 0 || strcasecmp(val, "On") == 0)) ? 1 : 0;

    val = zend_ini_string_ex("zend.assertions", sizeof("zend.assertions") - 1, 0, NULL);
    root->zend_assertions = val ? (int)atol(val) : -1;
}

void finalize_root_span(profiler_state_t *state)
{
    profiler_root_span_t *root = &state->root;
    if (!root->active) return;

    root->end_time_ns = realtime_ns();
    root->http_status_code = SG(sapi_headers).http_response_code;
    if (root->http_status_code == 0) root->http_status_code = 200;

    /* Set error status for 5xx */
    if (root->http_status_code >= 500) {
        root->status_code = SPAN_STATUS_ERROR;
    }

    /* Peak memory usage */
    root->peak_memory_bytes = (long)zend_memory_peak_usage(1);

    /* Check for uncaught exception */
    if (EG(exception)) {
        root->status_code = SPAN_STATUS_ERROR;
        zend_object *ex = EG(exception);
        zval rv;
        zval *msg = zend_read_property_ex(ex->ce, ex,
                        ZSTR_KNOWN(ZEND_STR_MESSAGE), 1, &rv);
        if (msg && Z_TYPE_P(msg) == IS_STRING) {
            snprintf(root->status_message, sizeof(root->status_message),
                     "%s", Z_STRVAL_P(msg));
        }
    }

    root->active = 0;
}
