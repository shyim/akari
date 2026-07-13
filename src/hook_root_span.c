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

static int hex_nibble(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

/**
 * Parse W3C traceparent header: "00-{32hex}-{16hex}-{2hex}"
 * Returns 1 if valid, populates trace_id, parent_id and the sampled bit of
 * the trace-flags byte.
 */
static int parse_traceparent(const char *header, char *trace_id_out, char *parent_id_out,
                             int *sampled_out)
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
    /* trace-flags: 2 chars; bit 0 = sampled */
    if (!is_valid_hex(header + 53, 2)) return 0;

    memcpy(trace_id_out, header + 3, 32);
    memcpy(parent_id_out, header + 36, 16);
    *sampled_out = hex_nibble(header[54]) & 0x01;
    return 1;
}

/* Capture PHP runtime/INI annotations shared by every root span (web and CLI):
 * version, SAPI, OPcache and a handful of runtime limits. */
static void init_root_runtime_annotations(profiler_root_span_t *root)
{
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

/* Append one CLI argument to dst (a "cmd arg arg ..." accumulator), space-
 * separated, never overflowing cap. Returns the new length. */
static size_t append_cli_arg(char *dst, size_t len, size_t cap, const char *arg)
{
    if (len + 1 >= cap) return len;
    if (len > 0) dst[len++] = ' ';
    size_t alen = strlen(arg);
    if (len + alen >= cap) alen = cap - 1 - len;
    memcpy(dst + len, arg, alen);
    len += alen;
    dst[len] = '\0';
    return len;
}

/* Build the CLI command line and span name from the PHP binary and argv.
 *
 * process.command_line is the full command as the OS sees it: the PHP executable
 * followed by every argv element verbatim, e.g.
 *   "/usr/bin/php /var/www/bin/console asset:install".
 *
 * The span name stays readable and low-cardinality: "php <script-basename>
 * <args>", e.g. "php console asset:install". Falls back to "php CLI" when there
 * is no script/argv (e.g. `php -r`, interactive). */
static void cli_span_name(profiler_root_span_t *root)
{
    const char *script = SG(request_info).path_translated;
    int argc = SG(request_info).argc;
    char **argv = SG(request_info).argv;
    const char *php_bin = PG(php_binary);

    /* process.command_line: real executable path + full argv (script path + args). */
    char *cmd = root->process_command_line;
    size_t cmdcap = sizeof(root->process_command_line);
    size_t cmdlen = append_cli_arg(cmd, 0, cmdcap, (php_bin && php_bin[0]) ? php_bin : "php");
    for (int i = 0; i < argc && argv && argv[i]; i++) {
        cmdlen = append_cli_arg(cmd, cmdlen, cmdcap, argv[i]);
    }

    /* Span name: "php <basename> <args>" — basename keeps the name readable. */
    char name[ROOT_ATTR_MAX];
    size_t namelen = 0;
    if (script && script[0]) {
        const char *base = strrchr(script, '/');
        base = base ? base + 1 : script;
        namelen = append_cli_arg(name, 0, sizeof(name), base);
        snprintf(root->url_path, sizeof(root->url_path), "%s", script);
    }
    for (int i = 1; i < argc && argv && argv[i]; i++) {
        namelen = append_cli_arg(name, namelen, sizeof(name), argv[i]);
    }

    if (namelen > 0) {
        int n = snprintf(root->name, sizeof(root->name), "php %s", name);
        root->name_len = profiler_clamp_snprintf_len(n, sizeof(root->name));
    } else {
        memcpy(root->name, "php CLI", 7);
        root->name_len = 7;
    }
    root->name_is_auto = 1;
}

void init_root_span(profiler_state_t *state)
{
    profiler_root_span_t *root = &state->root;
    memset(root, 0, sizeof(profiler_root_span_t));

    /* Detect SAPI type */
    root->is_cli = (strcmp(sapi_module.name, "cli") == 0);

    if (root->is_cli) {
        /* CLI trace context comes from the TRACEPARENT environment variable
         * (the W3C convention for process propagation). Parsed before the
         * trace_cli gate so parent-based sampling and trace-id adoption work
         * even when the auto CLI root span is disabled. */
        const char *env_tp = getenv("TRACEPARENT");
        if (env_tp && parse_traceparent(env_tp, state->trace_id, root->parent_span_id,
                                        &root->parent_sampled)) {
            root->has_parent = 1;
        }

        /* Without auto CLI tracing, leave the root inactive: individual
         * instrumented calls still emit spans, but there is no entry span
         * unless userland calls markAsWebTransaction(). */
        if (!AKARI_G(trace_cli)) {
            root->active = 0;
            return;
        }

        /* Auto-trace the CLI invocation as an INTERNAL root span named after
         * the script being run. */
        profiler_generate_hex_id(state, root->span_id, 16);
        root->start_time_ns = realtime_ns();
        root->status_code = SPAN_STATUS_UNSET;
        root->kind = SPAN_KIND_INTERNAL;
        root->active = 1;
        cli_span_name(root);
        init_root_runtime_annotations(root);
        return;
    }

    /* Generate root span ID */
    profiler_generate_hex_id(state, root->span_id, 16);
    root->start_time_ns = realtime_ns();
    root->status_code = SPAN_STATUS_UNSET;
    root->kind = SPAN_KIND_SERVER;
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
        int n = snprintf(root->name, sizeof(root->name), "%s %s",
                          root->http_method, root->url_path);
        root->name_len = profiler_clamp_snprintf_len(n, sizeof(root->name));
    } else if (root->http_method[0]) {
        int n = snprintf(root->name, sizeof(root->name), "%s", root->http_method);
        root->name_len = profiler_clamp_snprintf_len(n, sizeof(root->name));
    } else {
        memcpy(root->name, "HTTP", 4);
        root->name_len = 4;
    }

    /* Try to read traceparent header */
    const char *traceparent = sapi_getenv("HTTP_TRACEPARENT", 16);
    if (traceparent && parse_traceparent(traceparent, state->trace_id, root->parent_span_id,
                                         &root->parent_sampled)) {
        root->has_parent = 1;
        /* trace_id is now from the incoming traceparent */
    }

    /* Runtime environment annotations */
    init_root_runtime_annotations(root);
}

void promote_root_to_web(profiler_state_t *state)
{
    profiler_root_span_t *root = &state->root;

    /* No-op if the root is already an active web transaction. */
    if (!root->is_cli && root->active) return;

    /* Promote a CLI root to a web (SERVER) transaction. With auto CLI tracing
     * the root is already active (INTERNAL kind, named after the script); with
     * it disabled init_root_span leaves the root inactive and without an
     * id/start time/name. Either way, give it the identity, kind and timing a
     * request root needs so it is finalized and exported like any HTTP root. */
    root->is_cli = 0;
    root->kind = SPAN_KIND_SERVER;
    if (!root->active) {
        profiler_generate_hex_id(state, root->span_id, 16);
        root->start_time_ns = realtime_ns();
        root->status_code = SPAN_STATUS_UNSET;
        root->active = 1;
        /* The inactive CLI path (akari.trace_cli=0) never ran these, so the root
         * would otherwise export with empty php.version / php.sapi. */
        init_root_runtime_annotations(root);
        /* A name set via setTransactionName() while the root was inactive was
         * only stored in custom_transaction_name; apply it now. */
        if (state->has_custom_transaction && state->custom_transaction_name[0]) {
            size_t len = strlen(state->custom_transaction_name);
            if (len >= ROOT_ATTR_MAX) len = ROOT_ATTR_MAX - 1;
            memcpy(root->name, state->custom_transaction_name, len);
            root->name[len] = '\0';
            root->name_len = len;
        }
    }
    /* Drop the auto-derived CLI name so the web default applies, but preserve a
     * name set explicitly via setTransactionName(). */
    if (root->name_is_auto) {
        root->name[0] = '\0';
        root->name_len = 0;
        root->url_path[0] = '\0';
        root->name_is_auto = 0;
    }
    if (!root->name[0]) {
        memcpy(root->name, "CLI", 3);
        root->name_len = 3;
    }
}

void finalize_root_span(profiler_state_t *state)
{
    profiler_root_span_t *root = &state->root;
    if (!root->active) return;

    root->end_time_ns = realtime_ns();

    /* HTTP status only applies to web transactions; a CLI root has no response
     * code, so leave http_status_code at 0 for it. */
    if (!root->is_cli) {
        root->http_status_code = SG(sapi_headers).http_response_code;
        if (root->http_status_code == 0) root->http_status_code = 200;

        /* Set error status for 5xx */
        if (root->http_status_code >= 500) {
            root->status_code = SPAN_STATUS_ERROR;
        }
    }

    /* Peak memory usage */
    root->peak_memory_bytes = (long)zend_memory_peak_usage(1);

    /* Check for uncaught exception. EG(exception) is still set if we finalize
     * mid-unwind, but the engine clears it before request shutdown — so also
     * honor root_exception_escaped, recorded during unwind in
     * profiler_mark_escaped_exceptions for exceptions that escaped every frame
     * (including throws with no hooked span on the stack). An exit()/die()
     * sentinel unwinds the same way but is not an error — ignore it. */
    if (EG(exception) && !zend_is_unwind_exit(EG(exception)) && !zend_is_graceful_exit(EG(exception))) {
        root->status_code = SPAN_STATUS_ERROR;
        zend_object *ex = EG(exception);
        zval rv;
        zval *msg = zend_read_property_ex(ex->ce, ex,
                        ZSTR_KNOWN(ZEND_STR_MESSAGE), 1, &rv);
        if (msg && Z_TYPE_P(msg) == IS_STRING) {
            snprintf(root->status_message, sizeof(root->status_message),
                     "%s", Z_STRVAL_P(msg));
        }
    } else if (state->root_exception_escaped) {
        root->status_code = SPAN_STATUS_ERROR;
        if (state->root_exception_message[0] && !root->status_message[0]) {
            snprintf(root->status_message, sizeof(root->status_message),
                     "%s", state->root_exception_message);
        }
    }

    /* CLI: a non-zero process exit code means the command failed, even when the
     * underlying exception was caught and handled by the framework (e.g. Symfony
     * console logs the error and calls exit(1)). Reflect that as an error on the
     * root span unless an uncaught exception already set a more specific status. */
    if (root->is_cli && root->status_code != SPAN_STATUS_ERROR && EG(exit_status) != 0) {
        root->status_code = SPAN_STATUS_ERROR;
        if (!root->status_message[0]) {
            snprintf(root->status_message, sizeof(root->status_message),
                     "exited with code %d", EG(exit_status));
        }
    }

    root->active = 0;
}

/* ── Trace sampling (OTEL_TRACES_SAMPLER semantics) ── */

typedef enum {
    AKARI_SAMPLER_ALWAYS_ON = 0,
    AKARI_SAMPLER_ALWAYS_OFF,
    AKARI_SAMPLER_TRACEIDRATIO,
    AKARI_SAMPLER_PARENTBASED_ALWAYS_ON,
    AKARI_SAMPLER_PARENTBASED_ALWAYS_OFF,
    AKARI_SAMPLER_PARENTBASED_TRACEIDRATIO,
} akari_sampler_mode_t;

static const char *sampler_mode_names[] = {
    "always_on",
    "always_off",
    "traceidratio",
    "parentbased_always_on",
    "parentbased_always_off",
    "parentbased_traceidratio",
};

/* Resolve the sampler: akari.traces_sampler INI when set, else the
 * OTEL_TRACES_SAMPLER env var, else parentbased_always_on (the OTel SDK
 * default: follow the parent's sampled flag, trace root requests). An
 * unrecognized name warns and falls back to that same default. */
static akari_sampler_mode_t resolve_sampler(double *ratio_out)
{
    const char *name = AKARI_G(traces_sampler);
    if (!name || !name[0]) {
        name = getenv("OTEL_TRACES_SAMPLER");
    }

    const char *arg = AKARI_G(traces_sampler_arg);
    if (!arg || !arg[0]) {
        arg = getenv("OTEL_TRACES_SAMPLER_ARG");
    }
    /* Ratio argument for the traceidratio samplers; OTel default is 1.0. */
    double ratio = 1.0;
    if (arg && arg[0]) {
        ratio = strtod(arg, NULL);
        if (ratio < 0.0) ratio = 0.0;
        if (ratio > 1.0) ratio = 1.0;
    }
    *ratio_out = ratio;

    if (!name || !name[0]) {
        return AKARI_SAMPLER_PARENTBASED_ALWAYS_ON;
    }
    for (size_t i = 0; i < sizeof(sampler_mode_names) / sizeof(sampler_mode_names[0]); i++) {
        if (strcmp(name, sampler_mode_names[i]) == 0) {
            return (akari_sampler_mode_t)i;
        }
    }
    php_error_docref(NULL, E_WARNING,
                     "unsupported traces sampler \"%s\", falling back to parentbased_always_on", name);
    return AKARI_SAMPLER_PARENTBASED_ALWAYS_ON;
}

const char *akari_sampler_effective_name(void)
{
    double ratio;
    return sampler_mode_names[resolve_sampler(&ratio)];
}

/* Consistent probability decision from the trace id: compare its lowest
 * 56 bits (the portion W3C Trace Context designates as random) against
 * ratio * 2^56. Services sharing a trace id reach the same verdict. */
static int trace_id_ratio_sampled(const char *trace_id, double ratio)
{
    if (ratio >= 1.0) return 1;
    if (ratio <= 0.0) return 0;

    uint64_t value = 0;
    for (int i = 18; i < 32; i++) {
        value = (value << 4) | (uint64_t)hex_nibble(trace_id[i]);
    }
    uint64_t threshold = (uint64_t)(ratio * (double)(1ULL << 56));
    return value < threshold;
}

int akari_sampling_decide(profiler_state_t *state)
{
    double ratio;
    akari_sampler_mode_t mode = resolve_sampler(&ratio);
    int has_parent = state->root.has_parent;
    int parent_sampled = state->root.parent_sampled;

    switch (mode) {
        case AKARI_SAMPLER_ALWAYS_ON:
            return 1;
        case AKARI_SAMPLER_ALWAYS_OFF:
            return 0;
        case AKARI_SAMPLER_TRACEIDRATIO:
            return trace_id_ratio_sampled(state->trace_id, ratio);
        case AKARI_SAMPLER_PARENTBASED_ALWAYS_ON:
            return has_parent ? parent_sampled : 1;
        case AKARI_SAMPLER_PARENTBASED_ALWAYS_OFF:
            return has_parent ? parent_sampled : 0;
        case AKARI_SAMPLER_PARENTBASED_TRACEIDRATIO:
            return has_parent ? parent_sampled
                              : trace_id_ratio_sampled(state->trace_id, ratio);
    }
    return 1;
}
