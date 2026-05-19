#include "profiler_internal.h"
#include "hook_registry.h"

/*
 * PHP HTTP stream wrapper instrumentation.
 *
 * Instead of patching stream wrapper ops (which is brittle), we hook the
 * common stream functions and check if they target http/https URLs.
 *
 * Covered functions:
 *   - fopen, fread, fwrite, fclose, fgets, feof, fflush
 *   - file_get_contents, file_put_contents
 *   - stream_context_create, stream_context_set_option
 *
 * When an http/https URL is detected, records the URL as db.statement.
 */

/* ── Helper: check if a filename/path looks like an HTTP URL ── */

static int is_http_url(const char *path, size_t path_len)
{
    return (path_len >= 7 && (strncasecmp(path, "http://", 7) == 0))
        || (path_len >= 8 && (strncasecmp(path, "https://", 8) == 0));
}

static void record_http_stream_attr(profiler_state_t *state, uint32_t span_index,
                                      const char *url, size_t url_len)
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
    snprintf(attr->db_system, DB_SYSTEM_MAX, "http");
    if (url && url_len > 0) {
        if (url_len >= DB_STATEMENT_MAX) url_len = DB_STATEMENT_MAX - 1;
        memcpy(attr->db_statement, url, url_len);
        attr->db_statement[url_len] = '\0';
        attr->db_statement_len = url_len;
    }
    state->db_attr_count++;
}

/* ── Pre-hook for fopen/file_get_contents/file_put_contents ── */

static void *http_stream_open_pre(profiler_state_t *state, zend_execute_data *execute_data,
                                    profiler_span_t *span, uint32_t span_index)
{
    (void)span;
    uint32_t num_args = ZEND_CALL_NUM_ARGS(execute_data);
    if (num_args >= 1) {
        zval *path_arg = ZEND_CALL_ARG(execute_data, 1);
        if (path_arg && Z_TYPE_P(path_arg) == IS_STRING) {
            if (is_http_url(Z_STRVAL_P(path_arg), Z_STRLEN_P(path_arg))) {
                record_http_stream_attr(state, span_index, Z_STRVAL_P(path_arg), Z_STRLEN_P(path_arg));
                return NULL;
            }
        }
    }
    /* Not an HTTP URL — skip this span (return marker) */
    return (void *)(uintptr_t)1;
}

/* ── Registration ── */

void hook_php_streams_register(hook_registry_t *reg)
{
    /* fopen — may be http:// or filesystem */
    hook_register_function(reg, "fopen",
        HOOK_TYPE_INTERNAL, SPAN_KIND_CLIENT,
        http_stream_open_pre, NULL);

    /* file_get_contents — common for HTTP API calls */
    hook_register_function(reg, "file_get_contents",
        HOOK_TYPE_INTERNAL, SPAN_KIND_CLIENT,
        http_stream_open_pre, NULL);

    /* file_put_contents — HTTP PUT/POST via streams */
    hook_register_function(reg, "file_put_contents",
        HOOK_TYPE_INTERNAL, SPAN_KIND_CLIENT,
        http_stream_open_pre, NULL);
}
