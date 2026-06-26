#include "profiler_internal.h"
#include "hook_registry.h"

#include <curl/curl.h>

/* ── curl class resolution (lazy — curl may be a shared extension) ──
 * The resolved CurlHandle class entry lives in module globals (per-thread under
 * ZTS); curl_ce_resolved gates the one-time lookup for this thread. */
static zend_class_entry *get_curl_ce(void)
{
    if (!AKARI_G(curl_ce_resolved)) {
        AKARI_G(curl_ce_resolved) = 1;
        zend_string *name = zend_string_init("CurlHandle", 10, 0);
        AKARI_G(curl_ce) = zend_lookup_class(name);
        zend_string_release(name);
    }
    return AKARI_G(curl_ce);
}

/* ── curl handle access ── */

static inline CURL *get_curl_handle_from_zval(zval *zv)
{
    if (!zv || Z_TYPE_P(zv) != IS_OBJECT) return NULL;
    zend_class_entry *ce = get_curl_ce();
    if (!ce || !instanceof_function(Z_OBJCE_P(zv), ce)) return NULL;
    zend_object *obj = Z_OBJ_P(zv);
    char *base = ((char *)obj) - obj->handlers->offset;
    return *((CURL **)base);
}

/* ── User header tracking ──
 *
 * We track user-set CURLOPT_HTTPHEADER per curl handle so we can
 * append traceparent without clobbering user headers.
 *
 * Storage: HashTable mapping zend_object* (as uintptr_t key) → zval array.
 * Populated by curl_setopt side-effect, consumed by inject_traceparent().
 */

/* Per-request header tracking lives in module globals so it is per-thread
 * under ZTS (concurrent requests no longer share one table). Aliased here to
 * the original names to keep the body unchanged. */
#define curl_user_headers      AKARI_G(curl_user_headers)

/* ── Restored-header slist ownership ──
 *
 * After curl_exec we rebuild the user's CURLOPT_HTTPHEADER list and hand it
 * back to the handle. curl does NOT copy a slist passed to CURLOPT_HTTPHEADER —
 * it keeps the pointer and references it on the next request, so we cannot free
 * the list when we set it. We therefore own it per handle: the next exec on the
 * same handle frees the previous list before installing a fresh one, and any
 * still-installed lists are freed at request shutdown. Keyed by zend_object*.
 */
#define curl_restored_headers  AKARI_G(curl_restored_headers)

static void free_restored_slist(zval *zv)
{
    struct curl_slist *list = Z_PTR_P(zv);
    if (list) curl_slist_free_all(list);
}

static void curl_headers_init(void)
{
    if (!curl_user_headers) {
        ALLOC_HASHTABLE(curl_user_headers);
        zend_hash_init(curl_user_headers, 8, NULL, ZVAL_PTR_DTOR, 0);
    }
    if (!curl_restored_headers) {
        ALLOC_HASHTABLE(curl_restored_headers);
        zend_hash_init(curl_restored_headers, 8, NULL, free_restored_slist, 0);
    }
}

static void curl_headers_shutdown(void)
{
    /* Intentionally frees nothing. This runs on the profiler-disable path
     * (Akari\disable()) too, where the request continues and may re-enable:
     *  - Freeing curl_user_headers here and re-allocating on the next enable
     *    leaks the table when disable→enable happens within one request.
     *  - Freeing curl_restored_headers here would dangle: the user's CurlHandle
     *    objects are still alive with those slists installed via
     *    CURLOPT_HTTPHEADER (curl does not copy them), so a later curl_exec($ch)
     *    while disabled would dereference freed memory.
     * Both tables are per-request and are torn down once at engine RSHUTDOWN by
     * curl_propagation_request_end(), after PHP has destroyed the request's
     * objects so no handle can reference the slists. */
}

/* Free both per-request header tables. Only safe at engine RSHUTDOWN, after the
 * request's CurlHandle objects have been destroyed. */
static void curl_headers_free_all(void)
{
    if (curl_user_headers) {
        zend_hash_destroy(curl_user_headers);
        FREE_HASHTABLE(curl_user_headers);
        curl_user_headers = NULL;
    }
    if (curl_restored_headers) {
        zend_hash_destroy(curl_restored_headers);
        FREE_HASHTABLE(curl_restored_headers);
        curl_restored_headers = NULL;
    }
}

/* Install a freshly-built restored slist for this handle, freeing any list we
 * previously owned for it. Takes ownership of `list`. */
static void set_restored_slist(zval *handle_zval, struct curl_slist *list)
{
    if (!curl_restored_headers || !handle_zval || Z_TYPE_P(handle_zval) != IS_OBJECT) {
        /* No place to track it — free now to avoid a leak. Safe because the
         * caller has not yet handed it to curl in this path. */
        if (list) curl_slist_free_all(list);
        return;
    }
    uintptr_t key = (uintptr_t)Z_OBJ_P(handle_zval);
    if (list) {
        /* update_ptr runs the dtor on any existing entry, freeing the
         * previously-installed list before we replace it. (It asserts a
         * non-NULL pointer, hence the NULL case is handled separately below.) */
        zend_hash_index_update_ptr(curl_restored_headers, key, list);
    } else {
        /* Nothing to install — drop any list we previously owned for this
         * handle (the dtor frees it). del on a missing key is a no-op. */
        zend_hash_index_del(curl_restored_headers, key);
    }
}

static void store_user_headers(zval *handle_zval, zval *headers_array)
{
    if (!curl_user_headers || !handle_zval || Z_TYPE_P(handle_zval) != IS_OBJECT) return;
    if (!headers_array || Z_TYPE_P(headers_array) != IS_ARRAY) return;

    uintptr_t key = (uintptr_t)Z_OBJ_P(handle_zval);
    zval copy;
    ZVAL_COPY(&copy, headers_array);
    zend_hash_index_update(curl_user_headers, key, &copy);
}

static zval *get_user_headers(zval *handle_zval)
{
    if (!curl_user_headers || !handle_zval || Z_TYPE_P(handle_zval) != IS_OBJECT) return NULL;
    uintptr_t key = (uintptr_t)Z_OBJ_P(handle_zval);
    return zend_hash_index_find(curl_user_headers, key);
}

/* ── Traceparent injection ── */

static void build_traceparent(profiler_state_t *state, const profiler_span_t *span, char *out, size_t out_size)
{
    snprintf(out, out_size, "traceparent: 00-%.32s-%.16s-01",
             state->trace_id, span->span_id);
}

static struct curl_slist *inject_traceparent(CURL *ch, zval *handle_zval,
                                              profiler_state_t *state,
                                              const profiler_span_t *span)
{
    struct curl_slist *headers = NULL;

    /* Copy user headers first */
    zval *user_hdrs = get_user_headers(handle_zval);
    if (user_hdrs && Z_TYPE_P(user_hdrs) == IS_ARRAY) {
        zval *entry;
        ZEND_HASH_FOREACH_VAL(Z_ARRVAL_P(user_hdrs), entry) {
            if (Z_TYPE_P(entry) == IS_STRING) {
                if (strncasecmp(Z_STRVAL_P(entry), "traceparent:", 12) != 0 &&
                    strncasecmp(Z_STRVAL_P(entry), "tracestate:", 11) != 0) {
                    headers = curl_slist_append(headers, Z_STRVAL_P(entry));
                }
            }
        } ZEND_HASH_FOREACH_END();
    }

    char traceparent[128];
    build_traceparent(state, span, traceparent, sizeof(traceparent));
    headers = curl_slist_append(headers, traceparent);

    curl_easy_setopt(ch, CURLOPT_HTTPHEADER, headers);
    return headers;
}

/* ── Side-effect: track curl_setopt(CURLOPT_HTTPHEADER) ── */

static void curl_setopt_side_effect(profiler_state_t *state, zend_execute_data *execute_data)
{
    (void)state;
    if (!execute_data || !execute_data->func) return;

    uint32_t num_args = ZEND_CALL_NUM_ARGS(execute_data);
    if (num_args < 3) return;

    zval *handle_arg = ZEND_CALL_ARG(execute_data, 1);
    zval *option_arg = ZEND_CALL_ARG(execute_data, 2);
    zval *value_arg = ZEND_CALL_ARG(execute_data, 3);

    if (Z_TYPE_P(option_arg) != IS_LONG) return;
    if (Z_LVAL_P(option_arg) != CURLOPT_HTTPHEADER) return;

    curl_headers_init();
    store_user_headers(handle_arg, value_arg);
}

/* ── Side-effect: track curl_setopt_array($ch, [CURLOPT_HTTPHEADER => [...]]) ──
 *
 * Symfony HttpClient (and other clients) set all options — including
 * CURLOPT_HTTPHEADER — in a single curl_setopt_array() call rather than via
 * individual curl_setopt() calls. Without this hook, get_user_headers() returns
 * nothing on curl_exec, so inject_traceparent() installs an slist containing
 * only the traceparent and clobbers the user's Content-Type (curl then defaults
 * to application/x-www-form-urlencoded, which OpenSearch rejects with 406). */
static void curl_setopt_array_side_effect(profiler_state_t *state, zend_execute_data *execute_data)
{
    (void)state;
    if (!execute_data || !execute_data->func) return;

    uint32_t num_args = ZEND_CALL_NUM_ARGS(execute_data);
    if (num_args < 2) return;

    zval *handle_arg = ZEND_CALL_ARG(execute_data, 1);
    zval *options_arg = ZEND_CALL_ARG(execute_data, 2);

    if (Z_TYPE_P(options_arg) != IS_ARRAY) return;

    zval *headers = zend_hash_index_find(Z_ARRVAL_P(options_arg), CURLOPT_HTTPHEADER);
    if (!headers || Z_TYPE_P(headers) != IS_ARRAY) return;

    curl_headers_init();
    store_user_headers(handle_arg, headers);
}

/* ── Registry callbacks ── */

static void *curl_exec_pre(profiler_state_t *state, zend_execute_data *execute_data,
                            profiler_span_t *span, uint32_t span_index)
{
    (void)span_index;
    uint32_t num_args = ZEND_CALL_NUM_ARGS(execute_data);
    if (num_args < 1) return NULL;

    zval *handle_arg = ZEND_CALL_ARG(execute_data, 1);
    CURL *ch = get_curl_handle_from_zval(handle_arg);
    if (!ch) return NULL;

    curl_headers_init();
    struct curl_slist *injected = inject_traceparent(ch, handle_arg, state, span);

    /* Take ownership of the injected list immediately, in the same per-handle
     * slot used for restored lists. This is what makes us bailout-safe: if the
     * call below triggers zend_bailout (fatal error, timeout, exit) the post
     * hook never runs, but the list is still tracked and gets freed at request
     * shutdown by curl_propagation_request_end() instead of leaking. On the
     * normal path the post hook replaces this slot (freeing `injected`) with
     * the restored user headers. */
    set_restored_slist(handle_arg, injected);
    return injected;
}

static void curl_exec_post(profiler_state_t *state, zend_execute_data *execute_data,
                            zval *return_value, profiler_span_t *span,
                            uint32_t span_index, void *pre_data)
{
    (void)return_value;
    /* The injected list (pre_data) is owned by the per-handle slot in
     * curl_restored_headers (registered in curl_exec_pre for bailout safety).
     * We do NOT free it directly here — installing the restored list below via
     * set_restored_slist() replaces the slot and frees the injected list as
     * part of the replace, which avoids a double free. */
    struct curl_slist *injected = (struct curl_slist *)pre_data;

    /* Restore user headers */
    if (injected) {
        uint32_t num_args = ZEND_CALL_NUM_ARGS(execute_data);
        if (num_args >= 1) {
            zval *handle_arg = ZEND_CALL_ARG(execute_data, 1);
            CURL *ch = get_curl_handle_from_zval(handle_arg);
            if (ch) {
                zval *user_hdrs = get_user_headers(handle_arg);
                if (user_hdrs && Z_TYPE_P(user_hdrs) == IS_ARRAY) {
                    struct curl_slist *restored = NULL;
                    zval *entry;
                    ZEND_HASH_FOREACH_VAL(Z_ARRVAL_P(user_hdrs), entry) {
                        if (Z_TYPE_P(entry) == IS_STRING) {
                            restored = curl_slist_append(restored, Z_STRVAL_P(entry));
                        }
                    } ZEND_HASH_FOREACH_END();
                    curl_easy_setopt(ch, CURLOPT_HTTPHEADER, restored);
                    /* Replaces the injected list in the slot (freeing it); curl
                     * references `restored` until the next request, and we free
                     * it on the next exec or at request shutdown. */
                    set_restored_slist(handle_arg, restored);
                } else {
                    curl_easy_setopt(ch, CURLOPT_HTTPHEADER, NULL);
                    /* Drop the injected list we owned for this handle. */
                    set_restored_slist(handle_arg, NULL);
                }
            }
        }
    }

    /* Record attributes */
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

    uint32_t num_args = ZEND_CALL_NUM_ARGS(execute_data);
    if (num_args < 1) goto done;

    zval *handle_arg = ZEND_CALL_ARG(execute_data, 1);
    CURL *ch = get_curl_handle_from_zval(handle_arg);
    if (!ch) goto done;

    char *url = NULL;
    curl_easy_getinfo(ch, CURLINFO_EFFECTIVE_URL, &url);
    if (url) {
        size_t len = strlen(url);
        if (len >= PROFILER_HTTP_URL_MAX) len = PROFILER_HTTP_URL_MAX - 1;
        memcpy(attr->url, url, len);
        attr->url[len] = '\0';
        attr->url_len = len;

        const char *p = url;
        if (strncmp(p, "http://", 7) == 0) p += 7;
        else if (strncmp(p, "https://", 8) == 0) p += 8;

        const char *slash = strchr(p, '/');
        const char *colon = strchr(p, ':');
        if (colon && (!slash || colon < slash)) {
            size_t hlen = (size_t)(colon - p);
            if (hlen < sizeof(attr->server_address)) {
                memcpy(attr->server_address, p, hlen);
                attr->server_address[hlen] = '\0';
            }
            attr->server_port = (uint16_t)atoi(colon + 1);
        } else if (slash) {
            size_t hlen = (size_t)(slash - p);
            if (hlen < sizeof(attr->server_address)) {
                memcpy(attr->server_address, p, hlen);
                attr->server_address[hlen] = '\0';
            }
        } else {
            snprintf(attr->server_address, sizeof(attr->server_address), "%s", p);
        }
    }

    char *method = NULL;
    curl_easy_getinfo(ch, CURLINFO_EFFECTIVE_METHOD, &method);
    if (method) {
        snprintf(attr->method, sizeof(attr->method), "%s", method);
    }

    long http_code = 0;
    curl_easy_getinfo(ch, CURLINFO_RESPONSE_CODE, &http_code);
    attr->http_status_code = (int)http_code;

    if (http_code >= 500) {
        span->status_code = SPAN_STATUS_ERROR;
    }

done:
    state->http_attr_count++;
}

/* ── Lifecycle ── */

void curl_propagation_rinit(void)
{
    curl_headers_init();
}

void curl_propagation_rshutdown(void)
{
    curl_headers_shutdown();
}

/* Called only from PHP_RSHUTDOWN, after the request's CurlHandle objects are
 * gone. Frees both per-request header tables. Kept separate from
 * curl_propagation_rshutdown() because that path also runs on Akari\disable(),
 * where handles are still live (see curl_headers_shutdown). */
void curl_propagation_request_end(void)
{
    curl_headers_free_all();
}

/* ── curl_multi_exec hook ── */

static void *curl_multi_exec_pre(profiler_state_t *state, zend_execute_data *execute_data,
                                  profiler_span_t *span, uint32_t span_index)
{
    (void)state;
    (void)execute_data;
    (void)span;
    (void)span_index;
    return NULL;
}

/* ── Registration ── */

void hook_curl_register(hook_registry_t *reg)
{
    hook_register_function(reg, "curl_exec",
        HOOK_TYPE_INTERNAL, SPAN_KIND_CLIENT, curl_exec_pre, curl_exec_post);
    hook_register_function(reg, "curl_multi_exec",
        HOOK_TYPE_INTERNAL, SPAN_KIND_CLIENT, curl_multi_exec_pre, NULL);

    hook_register_side_effect(reg, "curl_setopt", curl_setopt_side_effect);
    hook_register_side_effect(reg, "curl_setopt_array", curl_setopt_array_side_effect);
}
