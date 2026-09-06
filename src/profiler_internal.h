#ifndef PROFILER_INTERNAL_H
#define PROFILER_INTERNAL_H

/*
 * Shared internal declarations between profiler source files.
 * NOT part of the public API — only included by src/ .c files.
 */

#include "main/php.h"
#include "main/SAPI.h"
#include "Zend/zend_execute.h"
#include "Zend/zend_exceptions.h"
#include "profiler.h"
/* After profiler.h so profiler_state_t is fully defined; provides AKARI_G and
 * the g_state alias (module-global, per-thread under ZTS) to all hook files. */
#include "../include/php_akari.h"

#include <stdlib.h>
#include <string.h>
#include <time.h>

static inline size_t profiler_clamp_snprintf_len(int n, size_t buf_size)
{
    if (n <= 0 || buf_size == 0) {
        return 0;
    }
    return (size_t)n < buf_size ? (size_t)n : buf_size - 1;
}

/* Forward declaration */
struct hook_registry_t_tag;

/* ── Shared state ──
 * The per-request profiler state is module-global storage, reached as
 * AKARI_G(state) / the g_state alias (defined in php_akari.h, included above).
 * Per-thread under ZTS — no file-scope extern here. */

/* ── Observer API (defined in observer.c) ── */

void observer_register(void);

/* ── Shutdown lifecycle (defined in profiler.c) ── */

void profiler_rshutdown_finalize(void);

/* ── Time helpers (defined in profiler_span.c) ── */

uint64_t realtime_ns(void);
uint64_t monotonic_ns(void);

/* ── Span helpers (defined in profiler_span.c) ── */

size_t get_function_name(zend_execute_data *execute_data, char *buf, size_t buf_size);
void extract_code_attrs(zend_execute_data *execute_data,
    char *filepath, size_t filepath_size, size_t *filepath_len,
    char *function, size_t function_size, size_t *function_len,
    char *ns, size_t ns_size, size_t *ns_len,
    uint32_t *lineno);
uint32_t find_or_add_frame(profiler_state_t *state,
    const char *name, size_t name_len, uint32_t parent_index,
    const char *filepath, size_t filepath_len,
    const char *function, size_t function_len,
    const char *ns, size_t ns_len,
    uint32_t lineno);
int ensure_span_capacity(profiler_state_t *state);
void maybe_flush(profiler_state_t *state);
void maybe_flush_logs(profiler_state_t *state);
void profiler_drop_span(profiler_state_t *state, size_t span_index);
void profiler_compact_exported_spans(profiler_state_t *state);
void profiler_compact_exported_logs(profiler_state_t *state);

/* ── Attribute / event removal (defined in profiler_span.c) ── */

void profiler_remove_db_attrs(profiler_state_t *state, uint32_t span_index);
void profiler_remove_http_attrs(profiler_state_t *state, uint32_t span_index);
void profiler_remove_msg_attrs(profiler_state_t *state, uint32_t span_index);
void profiler_remove_template_attrs(profiler_state_t *state, uint32_t span_index);
void profiler_remove_exception_events(profiler_state_t *state, uint32_t span_index);
void profiler_remove_span_tags(profiler_state_t *state, uint32_t span_index);

/* ── Caught-exception detection (defined in profiler_span.c) ──
 * Engine-hook exception events start "pending". An exception's fate is only
 * known once it stops propagating: if the span it was thrown on has returned
 * and no exception is live, it was caught by application code; if it unwinds
 * past the outermost frame while still live, it is uncaught. */

/* Drop pending events that were caught: span has returned, no live exception.
 * No-op while an exception is still propagating. Safe to call before
 * serializing spans (getSpansJson) so introspection reflects caught vs. not. */
void profiler_settle_caught_exceptions(profiler_state_t *state);

/* Resolve a live exception against every pending event: promote the one whose
 * object matches EG(exception) (it escaped — marks span + root ERROR and
 * records the escape on the root independently of hooked spans) and drop the
 * rest (caught). With no live exception this drops all pending events, so it
 * also serves as the shutdown sweep for anything the unwind path missed. */
void profiler_mark_escaped_exceptions(profiler_state_t *state);

/* ── PDO class resolution (defined in hook_pdo.c) ── */

void profiler_resolve_pdo_classes(void);

/* ── #[Akari\Span] attribute support (defined in hook_attribute.c) ── */

void hook_attribute_register_class(void);
int hook_attribute_lookup(profiler_state_t *state, zend_execute_data *execute_data,
                          const char **out_name, uint32_t *out_name_len,
                          uint64_t *out_min_duration_ns);
void hook_attribute_cache_free(profiler_state_t *state);

/* ── curl lifecycle (defined in hook_curl.c) ── */

void curl_propagation_rinit(void);
void curl_propagation_rshutdown(void);
/* Free restored-header slists; safe only at engine RSHUTDOWN (handles gone). */
void curl_propagation_request_end(void);

/* ── Hook registration functions (each defined in their hook_*.c file) ── */
/* Include hook_registry.h to get the hook_registry_t type */

#include "hook_registry.h"

void hook_pdo_register(hook_registry_t *reg);
void hook_sqlite3_register(hook_registry_t *reg);
void sqlite3_rshutdown(void);
void hook_curl_register(hook_registry_t *reg);
void hook_redis_register(hook_registry_t *reg);
void hook_amqp_register(hook_registry_t *reg);
void hook_framework_register(hook_registry_t *reg);
void hook_twig_register(hook_registry_t *reg);
void hook_mysqli_register(hook_registry_t *reg);
void hook_io_register(hook_registry_t *reg);
void hook_memcached_register(hook_registry_t *reg);
void hook_doctrine_register(hook_registry_t *reg);
void hook_symfony_register(hook_registry_t *reg);
void hook_elasticsearch_register(hook_registry_t *reg);
void hook_predis_register(hook_registry_t *reg);
void hook_laravel_register(hook_registry_t *reg);
void hook_shopware_register(hook_registry_t *reg);
void hook_error_register(hook_registry_t *reg);
void hook_oci8_register(hook_registry_t *reg);
void hook_grpc_register(hook_registry_t *reg);
void hook_rdkafka_register(hook_registry_t *reg);
void hook_graphql_register(hook_registry_t *reg);
void hook_soap_register(hook_registry_t *reg);
void hook_pheanstalk_register(hook_registry_t *reg);
void hook_php_streams_register(hook_registry_t *reg);
void hook_pcre_register(hook_registry_t *reg);

/* ── Engine-level hooks (defined in hook_engine.c) ── */

void engine_hooks_init(int trace_compile, int trace_gc);
void engine_hooks_shutdown(void);
void profiler_capture_stacktrace(profiler_state_t *state, uint32_t span_index);

/* ── SQL normalization (defined in sql_normalize.c) ── */

void profiler_sql_normalize(const char *sql, size_t sql_len,
                              char *out, size_t out_size, size_t *out_len);
void profiler_sql_truncate(const char *sql, size_t sql_len,
                             char *out, size_t out_size, size_t *out_len);

/* ── Root span (defined in hook_root_span.c) ── */

void init_root_span(profiler_state_t *state);
void promote_root_to_web(profiler_state_t *state);
void finalize_root_span(profiler_state_t *state);

/* ── Trace sampling (defined in hook_root_span.c) ──
 * Evaluates the configured sampler (akari.traces_sampler INI, falling back to
 * the OTEL_TRACES_SAMPLER env var) against the request's trace context. Called
 * once per request after init_root_span; returns 0 to drop the request. */
int akari_sampling_decide(profiler_state_t *state);
/* Effective sampler name after INI/env resolution (for phpinfo). */
const char *akari_sampler_effective_name(void);

#endif /* PROFILER_INTERNAL_H */
