#include "profiler_internal.h"
#include "hook_registry.h"

/*
 * Simple I/O function instrumentation — covers common latency-hiding functions.
 *
 * These are all plain functions (no class), hooked via zend_execute_internal.
 * They create CLIENT or INTERNAL spans depending on semantics.
 *
 * Covered:
 *   Session:  session_start, session_write_close
 *   DNS:      gethostbyname, gethostbynamel, dns_get_record, checkdnsrr, getmxrr
 *   Sleep:    sleep, usleep, time_nanosleep
 *   File I/O: file_get_contents, file_put_contents, fopen, fread, fwrite, fgets,
 *             file_exists, is_dir, is_file, copy, rename, unlink, mkdir, rmdir,
 *             chmod, touch, stat, glob, and more
 *   Mail:     mail, mb_send_mail
 *   Shell:    exec, shell_exec, system, passthru, proc_open, proc_close,
 *             proc_get_status, popen, pclose
 *   Password: password_hash, password_verify
 *   APCu:     apcu_fetch, apcu_store, apcu_delete, apcu_add, apcu_inc, apcu_dec,
 *             apcu_cas, apcu_exists
 */

/* ── Generic pre-hook: records db.system with a category label ── */

static void record_io_attr(profiler_state_t *state, uint32_t span_index, const char *system)
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
    snprintf(attr->db_system, DB_SYSTEM_MAX, "%s", system);
    state->db_attr_count++;
}

static void record_io_attr_with_statement(profiler_state_t *state, uint32_t span_index,
                                           const char *system, const char *statement, size_t stmt_len)
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
    snprintf(attr->db_system, DB_SYSTEM_MAX, "%s", system);
    if (statement && stmt_len > 0) {
        if (stmt_len >= DB_STATEMENT_MAX) stmt_len = DB_STATEMENT_MAX - 1;
        memcpy(attr->db_statement, statement, stmt_len);
        attr->db_statement[stmt_len] = '\0';
        attr->db_statement_len = stmt_len;
    }
    state->db_attr_count++;
}

/* ── Session hooks ── */

static void *session_pre(profiler_state_t *state, zend_execute_data *execute_data,
                          profiler_span_t *span, uint32_t span_index)
{
    (void)execute_data;
    (void)span;
    record_io_attr(state, span_index, "session");
    return NULL;
}

/* ── DNS hooks ── */

static void *dns_pre(profiler_state_t *state, zend_execute_data *execute_data,
                      profiler_span_t *span, uint32_t span_index)
{
    (void)span;
    /* Record hostname as statement if available (first arg) */
    uint32_t num_args = ZEND_CALL_NUM_ARGS(execute_data);
    if (num_args >= 1) {
        zval *host_arg = ZEND_CALL_ARG(execute_data, 1);
        if (host_arg && Z_TYPE_P(host_arg) == IS_STRING) {
            record_io_attr_with_statement(state, span_index, "dns",
                Z_STRVAL_P(host_arg), Z_STRLEN_P(host_arg));
            return NULL;
        }
    }
    record_io_attr(state, span_index, "dns");
    return NULL;
}

/* ── Sleep hooks ── */

static void *sleep_pre(profiler_state_t *state, zend_execute_data *execute_data,
                        profiler_span_t *span, uint32_t span_index)
{
    (void)execute_data;
    (void)span;
    record_io_attr(state, span_index, "sleep");
    return NULL;
}

/* ── Scheme-aware stream hooks ── */

static void *fs_path_pre(profiler_state_t *state, zend_execute_data *execute_data,
                         profiler_span_t *span, uint32_t span_index);

static int is_http_url(const char *path, size_t path_len)
{
    return (path_len >= 7 && strncasecmp(path, "http://", 7) == 0)
        || (path_len >= 8 && strncasecmp(path, "https://", 8) == 0);
}

static void record_http_stream_attr(profiler_state_t *state, uint32_t span_index,
                                    const char *url, size_t url_len, const char *method)
{
    if (state->http_attr_count >= state->http_attr_capacity) {
        size_t new_cap = state->http_attr_capacity ? state->http_attr_capacity * 2 : 16;
        profiler_http_attr_t *new_attrs = realloc(state->http_attrs,
                                                   new_cap * sizeof(profiler_http_attr_t));
        if (!new_attrs) return;
        state->http_attrs = new_attrs;
        state->http_attr_capacity = new_cap;
    }

    profiler_http_attr_t *attr = &state->http_attrs[state->http_attr_count];
    memset(attr, 0, sizeof(profiler_http_attr_t));
    attr->span_index = span_index;
    snprintf(attr->method, sizeof(attr->method), "%s", method);

    if (url_len >= PROFILER_HTTP_URL_MAX) url_len = PROFILER_HTTP_URL_MAX - 1;
    memcpy(attr->url, url, url_len);
    attr->url[url_len] = '\0';
    attr->url_len = url_len;

    /* Extract host and optional port from the URL authority. */
    const char *authority = url + (strncasecmp(url, "https://", 8) == 0 ? 8 : 7);
    const char *authority_end = authority;
    while (*authority_end && *authority_end != '/' && *authority_end != '?'
            && *authority_end != '#') {
        authority_end++;
    }

    const char *port_sep = NULL;
    const char *host_end = authority_end;
    if (authority < authority_end && *authority == '[') {
        const char *bracket = memchr(authority, ']', (size_t)(authority_end - authority));
        if (bracket) {
            host_end = bracket + 1;
            if (host_end < authority_end && *host_end == ':') port_sep = host_end;
        }
    } else {
        port_sep = memchr(authority, ':', (size_t)(authority_end - authority));
        if (port_sep) host_end = port_sep;
    }

    size_t host_len = (size_t)(host_end - authority);
    if (host_len >= sizeof(attr->server_address)) host_len = sizeof(attr->server_address) - 1;
    if (host_len > 0) {
        memcpy(attr->server_address, authority, host_len);
        attr->server_address[host_len] = '\0';
    }
    if (port_sep && port_sep + 1 < authority_end) {
        attr->server_port = (uint16_t)atoi(port_sep + 1);
    }

    state->http_attr_count++;
}

static void mark_http_stream(profiler_state_t *state, profiler_span_t *span,
                             uint32_t span_index, const char *url, size_t url_len,
                             const char *method)
{
    record_http_stream_attr(state, span_index, url, url_len, method);
    span->kind = SPAN_KIND_CLIENT;

    /* These hooks are registered by the mixed I/O module, so correct the
     * current operation's layer when its path resolves to HTTP. */
    if (state->layer_stack_overflow == 0 && state->layer_stack_depth > 0) {
        state->layer_stack[state->layer_stack_depth - 1].layer = AKARI_LAYER_HTTP;
    }
}

static zval *stream_path_arg(zend_execute_data *execute_data)
{
    uint32_t num_args = ZEND_CALL_NUM_ARGS(execute_data);
    if (num_args >= 1) {
        zval *path_arg = ZEND_CALL_ARG(execute_data, 1);
        if (path_arg && Z_TYPE_P(path_arg) == IS_STRING) return path_arg;
    }
    return NULL;
}

static void *file_get_contents_pre(profiler_state_t *state, zend_execute_data *execute_data,
                                   profiler_span_t *span, uint32_t span_index)
{
    zval *path_arg = stream_path_arg(execute_data);
    if (path_arg && is_http_url(Z_STRVAL_P(path_arg), Z_STRLEN_P(path_arg))) {
        mark_http_stream(state, span, span_index, Z_STRVAL_P(path_arg),
                         Z_STRLEN_P(path_arg), "GET");
        return NULL;
    }

    /* Local file_get_contents calls are intentionally omitted entirely. */
    return HOOK_PRE_SKIP_SPAN;
}

static void *fopen_pre(profiler_state_t *state, zend_execute_data *execute_data,
                       profiler_span_t *span, uint32_t span_index)
{
    zval *path_arg = stream_path_arg(execute_data);
    if (path_arg && is_http_url(Z_STRVAL_P(path_arg), Z_STRLEN_P(path_arg))) {
        mark_http_stream(state, span, span_index, Z_STRVAL_P(path_arg),
                         Z_STRLEN_P(path_arg), "GET");
        return NULL;
    }

    span->min_duration_ns = 1000000; /* Keep local filesystem calls only above 1ms. */
    return fs_path_pre(state, execute_data, span, span_index);
}

static void *file_put_contents_pre(profiler_state_t *state, zend_execute_data *execute_data,
                                   profiler_span_t *span, uint32_t span_index)
{
    zval *path_arg = stream_path_arg(execute_data);
    if (path_arg && is_http_url(Z_STRVAL_P(path_arg), Z_STRLEN_P(path_arg))) {
        mark_http_stream(state, span, span_index, Z_STRVAL_P(path_arg),
                         Z_STRLEN_P(path_arg), "PUT");
        return NULL;
    }

    span->min_duration_ns = 1000000; /* Keep local filesystem calls only above 1ms. */
    return fs_path_pre(state, execute_data, span, span_index);
}

/* ── Mail hook ── */

static void *mail_pre(profiler_state_t *state, zend_execute_data *execute_data,
                       profiler_span_t *span, uint32_t span_index)
{
    (void)span;
    /* Record recipient as statement */
    uint32_t num_args = ZEND_CALL_NUM_ARGS(execute_data);
    if (num_args >= 1) {
        zval *to_arg = ZEND_CALL_ARG(execute_data, 1);
        if (to_arg && Z_TYPE_P(to_arg) == IS_STRING) {
            record_io_attr_with_statement(state, span_index, "mail",
                Z_STRVAL_P(to_arg), Z_STRLEN_P(to_arg));
            return NULL;
        }
    }
    record_io_attr(state, span_index, "mail");
    return NULL;
}

/* ── Shell hooks ── */

static void *shell_pre(profiler_state_t *state, zend_execute_data *execute_data,
                        profiler_span_t *span, uint32_t span_index)
{
    (void)span;
    /* Record command as statement (first arg) */
    uint32_t num_args = ZEND_CALL_NUM_ARGS(execute_data);
    if (num_args >= 1) {
        zval *cmd_arg = ZEND_CALL_ARG(execute_data, 1);
        if (cmd_arg && Z_TYPE_P(cmd_arg) == IS_STRING) {
            record_io_attr_with_statement(state, span_index, "shell",
                Z_STRVAL_P(cmd_arg), Z_STRLEN_P(cmd_arg));
            return NULL;
        }
    }
    record_io_attr(state, span_index, "shell");
    return NULL;
}

/* ── Filesystem hooks ──
 *
 * Most filesystem operations are fast for local files but slow for networked
 * mounts (NFS, SMB). Use a 1ms threshold so only slow ops create spans.
 */

static void *fs_path_pre(profiler_state_t *state, zend_execute_data *execute_data,
                           profiler_span_t *span, uint32_t span_index)
{
    (void)span;
    uint32_t num_args = ZEND_CALL_NUM_ARGS(execute_data);
    if (num_args >= 1) {
        zval *path_arg = ZEND_CALL_ARG(execute_data, 1);
        if (path_arg && Z_TYPE_P(path_arg) == IS_STRING) {
            record_io_attr_with_statement(state, span_index, "filesystem",
                Z_STRVAL_P(path_arg), Z_STRLEN_P(path_arg));
            return NULL;
        }
    }
    record_io_attr(state, span_index, "filesystem");
    return NULL;
}

static void *fs_two_path_pre(profiler_state_t *state, zend_execute_data *execute_data,
                               profiler_span_t *span, uint32_t span_index)
{
    (void)span;
    uint32_t num_args = ZEND_CALL_NUM_ARGS(execute_data);
    if (num_args >= 2) {
        zval *src = ZEND_CALL_ARG(execute_data, 1);
        zval *dst = ZEND_CALL_ARG(execute_data, 2);
        if (src && dst && Z_TYPE_P(src) == IS_STRING && Z_TYPE_P(dst) == IS_STRING) {
            char buf[PROFILER_HTTP_URL_MAX];
            int n = snprintf(buf, sizeof(buf), "%.*s -> %.*s",
                (int)Z_STRLEN_P(src), Z_STRVAL_P(src),
                (int)Z_STRLEN_P(dst), Z_STRVAL_P(dst));
            size_t buf_len = profiler_clamp_snprintf_len(n, sizeof(buf));
            record_io_attr_with_statement(state, span_index, "filesystem", buf, buf_len);
            return NULL;
        }
    }
    record_io_attr(state, span_index, "filesystem");
    return NULL;
}

/* ── Password hashing hooks ──
 *
 * password_hash is intentionally slow (bcrypt/argon2), so it's always traced.
 * password_verify uses a 1ms threshold.
 */

static void *password_pre(profiler_state_t *state, zend_execute_data *execute_data,
                            profiler_span_t *span, uint32_t span_index)
{
    (void)span;
    (void)execute_data;
    record_io_attr(state, span_index, "password");
    return NULL;
}

/* ── APCu hooks ── */

static void *apcu_pre(profiler_state_t *state, zend_execute_data *execute_data,
                       profiler_span_t *span, uint32_t span_index)
{
    (void)span;
    uint32_t num_args = ZEND_CALL_NUM_ARGS(execute_data);
    const char *fname = "apcu";
    if (execute_data && execute_data->func && execute_data->func->common.function_name) {
        fname = ZSTR_VAL(execute_data->func->common.function_name);
    }

    /* Most APCu functions take key as first arg */
    if (num_args >= 1) {
        zval *key_arg = ZEND_CALL_ARG(execute_data, 1);
        if (key_arg && Z_TYPE_P(key_arg) == IS_STRING) {
            char buf[256];
            int n = snprintf(buf, sizeof(buf), "%s %s", fname, Z_STRVAL_P(key_arg));
            size_t buf_len = profiler_clamp_snprintf_len(n, sizeof(buf));
            record_io_attr_with_statement(state, span_index, "apcu", buf, buf_len);
            return NULL;
        }
    }
    record_io_attr(state, span_index, "apcu");
    return NULL;
}

/* ── Extended shell hooks ── */

static void *popen_pre(profiler_state_t *state, zend_execute_data *execute_data,
                        profiler_span_t *span, uint32_t span_index)
{
    (void)span;
    uint32_t num_args = ZEND_CALL_NUM_ARGS(execute_data);
    if (num_args >= 1) {
        zval *cmd_arg = ZEND_CALL_ARG(execute_data, 1);
        if (cmd_arg && Z_TYPE_P(cmd_arg) == IS_STRING) {
            record_io_attr_with_statement(state, span_index, "shell",
                Z_STRVAL_P(cmd_arg), Z_STRLEN_P(cmd_arg));
            return NULL;
        }
    }
    record_io_attr(state, span_index, "shell");
    return NULL;
}

/* ── Extended mail hooks ── */

static void *mb_send_mail_pre(profiler_state_t *state, zend_execute_data *execute_data,
                                profiler_span_t *span, uint32_t span_index)
{
    (void)span;
    uint32_t num_args = ZEND_CALL_NUM_ARGS(execute_data);
    if (num_args >= 1) {
        zval *to_arg = ZEND_CALL_ARG(execute_data, 1);
        if (to_arg && Z_TYPE_P(to_arg) == IS_STRING) {
            record_io_attr_with_statement(state, span_index, "mail",
                Z_STRVAL_P(to_arg), Z_STRLEN_P(to_arg));
            return NULL;
        }
    }
    record_io_attr(state, span_index, "mail");
    return NULL;
}

/* ── fastcgi_finish_request / frankenphp_finish_request ──
 *
 * These functions flush the HTTP response to the client. After they return,
 * PHP continues executing (cleanup, deferred work) but the user has already
 * received the response. We finalize the root span here so the recorded
 * duration reflects the actual response time, not the total PHP execution time.
 *
 * Registered as side-effects: no span is created, we just snapshot the root span.
 */

static void finish_request_side_effect(profiler_state_t *state, zend_execute_data *execute_data)
{
    (void)execute_data;
    if (!state || !state->root.active) return;

    /* Finalize root span at this point — captures the real response time */
    finalize_root_span(state);
}

/* ── Registration ── */

void hook_io_register(hook_registry_t *reg)
{
    /* Session */
    hook_register_function(reg, "session_start",
        HOOK_TYPE_INTERNAL, SPAN_KIND_INTERNAL, session_pre, NULL);
    hook_register_function_threshold(reg, "session_write_close",
        HOOK_TYPE_INTERNAL, SPAN_KIND_INTERNAL, 1.0, session_pre, NULL);

    /* DNS */
    hook_register_function(reg, "gethostbyname",
        HOOK_TYPE_INTERNAL, SPAN_KIND_CLIENT, dns_pre, NULL);
    hook_register_function(reg, "gethostbynamel",
        HOOK_TYPE_INTERNAL, SPAN_KIND_CLIENT, dns_pre, NULL);
    hook_register_function(reg, "dns_get_record",
        HOOK_TYPE_INTERNAL, SPAN_KIND_CLIENT, dns_pre, NULL);
    hook_register_function(reg, "checkdnsrr",
        HOOK_TYPE_INTERNAL, SPAN_KIND_CLIENT, dns_pre, NULL);
    hook_register_function(reg, "getmxrr",
        HOOK_TYPE_INTERNAL, SPAN_KIND_CLIENT, dns_pre, NULL);

    /* Sleep */
    hook_register_function(reg, "sleep",
        HOOK_TYPE_INTERNAL, SPAN_KIND_INTERNAL, sleep_pre, NULL);
    hook_register_function(reg, "usleep",
        HOOK_TYPE_INTERNAL, SPAN_KIND_INTERNAL, sleep_pre, NULL);
    hook_register_function(reg, "time_nanosleep",
        HOOK_TYPE_INTERNAL, SPAN_KIND_INTERNAL, sleep_pre, NULL);

    /* Stream functions share scheme-aware hooks so the first registration owns
     * both HTTP client semantics and local-filesystem filtering. */
    hook_register_function(reg, "file_get_contents",
        HOOK_TYPE_INTERNAL, SPAN_KIND_INTERNAL, file_get_contents_pre, NULL);
    hook_register_function(reg, "fopen",
        HOOK_TYPE_INTERNAL, SPAN_KIND_INTERNAL, fopen_pre, NULL);
    hook_register_function(reg, "file_put_contents",
        HOOK_TYPE_INTERNAL, SPAN_KIND_INTERNAL, file_put_contents_pre, NULL);

    /* Mail */
    hook_register_function(reg, "mail",
        HOOK_TYPE_INTERNAL, SPAN_KIND_INTERNAL, mail_pre, NULL);
    hook_register_function(reg, "mb_send_mail",
        HOOK_TYPE_INTERNAL, SPAN_KIND_INTERNAL, mb_send_mail_pre, NULL);

    /* Shell execution */
    hook_register_function(reg, "exec",
        HOOK_TYPE_INTERNAL, SPAN_KIND_INTERNAL, shell_pre, NULL);
    hook_register_function(reg, "shell_exec",
        HOOK_TYPE_INTERNAL, SPAN_KIND_INTERNAL, shell_pre, NULL);
    hook_register_function(reg, "system",
        HOOK_TYPE_INTERNAL, SPAN_KIND_INTERNAL, shell_pre, NULL);
    hook_register_function(reg, "passthru",
        HOOK_TYPE_INTERNAL, SPAN_KIND_INTERNAL, shell_pre, NULL);
    hook_register_function(reg, "proc_open",
        HOOK_TYPE_INTERNAL, SPAN_KIND_INTERNAL, shell_pre, NULL);
    hook_register_function(reg, "proc_close",
        HOOK_TYPE_INTERNAL, SPAN_KIND_INTERNAL, popen_pre, NULL);
    hook_register_function(reg, "proc_get_status",
        HOOK_TYPE_INTERNAL, SPAN_KIND_INTERNAL, popen_pre, NULL);
    hook_register_function(reg, "popen",
        HOOK_TYPE_INTERNAL, SPAN_KIND_INTERNAL, popen_pre, NULL);
    hook_register_function(reg, "pclose",
        HOOK_TYPE_INTERNAL, SPAN_KIND_INTERNAL, popen_pre, NULL);

    /* Filesystem ops — 1ms threshold to only trace slow I/O */
    hook_register_function_threshold(reg, "fread",
        HOOK_TYPE_INTERNAL, SPAN_KIND_INTERNAL, 1.0, fs_path_pre, NULL);
    hook_register_function_threshold(reg, "fwrite",
        HOOK_TYPE_INTERNAL, SPAN_KIND_INTERNAL, 1.0, fs_path_pre, NULL);
    hook_register_function_threshold(reg, "fgets",
        HOOK_TYPE_INTERNAL, SPAN_KIND_INTERNAL, 1.0, fs_path_pre, NULL);
    hook_register_function_threshold(reg, "fgetcsv",
        HOOK_TYPE_INTERNAL, SPAN_KIND_INTERNAL, 1.0, fs_path_pre, NULL);
    hook_register_function_threshold(reg, "fflush",
        HOOK_TYPE_INTERNAL, SPAN_KIND_INTERNAL, 1.0, fs_path_pre, NULL);
    hook_register_function_threshold(reg, "flock",
        HOOK_TYPE_INTERNAL, SPAN_KIND_INTERNAL, 1.0, fs_path_pre, NULL);
    hook_register_function_threshold(reg, "fseek",
        HOOK_TYPE_INTERNAL, SPAN_KIND_INTERNAL, 1.0, fs_path_pre, NULL);
    hook_register_function_threshold(reg, "ftell",
        HOOK_TYPE_INTERNAL, SPAN_KIND_INTERNAL, 1.0, fs_path_pre, NULL);
    hook_register_function_threshold(reg, "feof",
        HOOK_TYPE_INTERNAL, SPAN_KIND_INTERNAL, 1.0, fs_path_pre, NULL);
    hook_register_function_threshold(reg, "fpassthru",
        HOOK_TYPE_INTERNAL, SPAN_KIND_INTERNAL, 1.0, fs_path_pre, NULL);
    hook_register_function_threshold(reg, "file_exists",
        HOOK_TYPE_INTERNAL, SPAN_KIND_INTERNAL, 1.0, fs_path_pre, NULL);
    hook_register_function_threshold(reg, "is_dir",
        HOOK_TYPE_INTERNAL, SPAN_KIND_INTERNAL, 1.0, fs_path_pre, NULL);
    hook_register_function_threshold(reg, "is_file",
        HOOK_TYPE_INTERNAL, SPAN_KIND_INTERNAL, 1.0, fs_path_pre, NULL);
    hook_register_function_threshold(reg, "is_readable",
        HOOK_TYPE_INTERNAL, SPAN_KIND_INTERNAL, 1.0, fs_path_pre, NULL);
    hook_register_function_threshold(reg, "is_writable",
        HOOK_TYPE_INTERNAL, SPAN_KIND_INTERNAL, 1.0, fs_path_pre, NULL);
    hook_register_function_threshold(reg, "is_executable",
        HOOK_TYPE_INTERNAL, SPAN_KIND_INTERNAL, 1.0, fs_path_pre, NULL);
    hook_register_function_threshold(reg, "is_link",
        HOOK_TYPE_INTERNAL, SPAN_KIND_INTERNAL, 1.0, fs_path_pre, NULL);
    hook_register_function_threshold(reg, "copy",
        HOOK_TYPE_INTERNAL, SPAN_KIND_INTERNAL, 1.0, fs_two_path_pre, NULL);
    hook_register_function_threshold(reg, "rename",
        HOOK_TYPE_INTERNAL, SPAN_KIND_INTERNAL, 1.0, fs_two_path_pre, NULL);
    hook_register_function_threshold(reg, "unlink",
        HOOK_TYPE_INTERNAL, SPAN_KIND_INTERNAL, 1.0, fs_path_pre, NULL);
    hook_register_function_threshold(reg, "mkdir",
        HOOK_TYPE_INTERNAL, SPAN_KIND_INTERNAL, 1.0, fs_path_pre, NULL);
    hook_register_function_threshold(reg, "rmdir",
        HOOK_TYPE_INTERNAL, SPAN_KIND_INTERNAL, 1.0, fs_path_pre, NULL);
    hook_register_function_threshold(reg, "chmod",
        HOOK_TYPE_INTERNAL, SPAN_KIND_INTERNAL, 1.0, fs_path_pre, NULL);
    hook_register_function_threshold(reg, "touch",
        HOOK_TYPE_INTERNAL, SPAN_KIND_INTERNAL, 1.0, fs_path_pre, NULL);
    hook_register_function_threshold(reg, "stat",
        HOOK_TYPE_INTERNAL, SPAN_KIND_INTERNAL, 1.0, fs_path_pre, NULL);
    hook_register_function_threshold(reg, "glob",
        HOOK_TYPE_INTERNAL, SPAN_KIND_INTERNAL, 1.0, fs_path_pre, NULL);
    hook_register_function_threshold(reg, "tmpfile",
        HOOK_TYPE_INTERNAL, SPAN_KIND_INTERNAL, 1.0, fs_path_pre, NULL);
    /* Password hashing — always traced (intentionally slow) */
    hook_register_function(reg, "password_hash",
        HOOK_TYPE_INTERNAL, SPAN_KIND_INTERNAL, password_pre, NULL);
    hook_register_function_threshold(reg, "password_verify",
        HOOK_TYPE_INTERNAL, SPAN_KIND_INTERNAL, 1.0, password_pre, NULL);

    /* APCu — fast local cache, 0.5ms threshold */
    hook_register_function_threshold(reg, "apcu_fetch",
        HOOK_TYPE_INTERNAL, SPAN_KIND_INTERNAL, 0.5, apcu_pre, NULL);
    hook_register_function_threshold(reg, "apcu_store",
        HOOK_TYPE_INTERNAL, SPAN_KIND_INTERNAL, 0.5, apcu_pre, NULL);
    hook_register_function_threshold(reg, "apcu_delete",
        HOOK_TYPE_INTERNAL, SPAN_KIND_INTERNAL, 0.5, apcu_pre, NULL);
    hook_register_function_threshold(reg, "apcu_add",
        HOOK_TYPE_INTERNAL, SPAN_KIND_INTERNAL, 0.5, apcu_pre, NULL);
    hook_register_function_threshold(reg, "apcu_inc",
        HOOK_TYPE_INTERNAL, SPAN_KIND_INTERNAL, 0.5, apcu_pre, NULL);
    hook_register_function_threshold(reg, "apcu_dec",
        HOOK_TYPE_INTERNAL, SPAN_KIND_INTERNAL, 0.5, apcu_pre, NULL);
    hook_register_function_threshold(reg, "apcu_cas",
        HOOK_TYPE_INTERNAL, SPAN_KIND_INTERNAL, 0.5, apcu_pre, NULL);
    hook_register_function_threshold(reg, "apcu_exists",
        HOOK_TYPE_INTERNAL, SPAN_KIND_INTERNAL, 0.5, apcu_pre, NULL);

    /* FPM/FrankenPHP response finalization — finalize root span at flush point */
    hook_register_side_effect(reg, "fastcgi_finish_request", finish_request_side_effect);
    hook_register_side_effect(reg, "frankenphp_finish_request", finish_request_side_effect);
}
