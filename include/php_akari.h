#ifndef PHP_AKARI_H
#define PHP_AKARI_H

#include "main/php.h"

#define PHP_AKARI_VERSION "0.1.0"
#define PHP_AKARI_EXTNAME "akari"
#define PHP_AKARI_NS "Akari"

#define ROOT_ATTR_MAX 256

/* Forward decls: full definitions live in profiler.h / hook_registry.h. Module
 * globals only hold pointers/embeds, so forward declarations keep this header
 * (the extension's public surface) free of the internal layout. */
typedef struct profiler_state_s profiler_state_t;
struct hook_ce_cache_t_fwd; /* see hook_registry.h: hook_ce_cache_t */

/* ── Module globals ──
 *
 * Declared here (rather than in php_akari.c) so every translation unit can
 * reach AKARI_G. Under ZTS these are per-thread, managed by TSRM; under NTS
 * they are a single static struct. Crucially `state` — the per-request
 * profiler state (spans, frames, attribute buffers) — lives HERE, so each
 * thread gets its own. It was previously a single process-wide `g_state`
 * pointer, which races across concurrent requests on a threaded SAPI.
 */
ZEND_BEGIN_MODULE_GLOBALS(akari)
    zend_bool enable;
    char *service_name;
    char pending_service_name[ROOT_ATTR_MAX];
    zend_long max_depth;
    double min_duration_ms;
    double event_dispatch_min_duration_ms;
    char *udp_host;
    zend_long udp_port;
    zend_bool trace_compile;
    zend_bool trace_gc;
    zend_bool trace_cli;
    zend_long flush_threshold;
    /* Trace sampler (OTel OTEL_TRACES_SAMPLER semantics). Empty INI falls back
     * to the OTEL_TRACES_SAMPLER / OTEL_TRACES_SAMPLER_ARG env vars, then to
     * parentbased_always_on (the OTel SDK default). */
    char *traces_sampler;
    char *traces_sampler_arg;
    /* Per-request profiler state; NULL until profiler_init() for the request. */
    profiler_state_t *state;
    /* Per-request hook scratch state (per-thread under ZTS). These hold curl
     * header tracking and sqlite prepared-statement SQL for the current
     * request; the owning hook files manage their lifecycle via accessors. */
    HashTable *curl_user_headers;
    HashTable *curl_restored_headers;
    HashTable *sqlite3_stmt_sql;
    /* Per-thread resolved-class-entry cache for the shared hook registry.
     * Opaque here (hook_ce_cache_t in hook_registry.h); allocated lazily. */
    struct hook_ce_cache_t_fwd *ce_cache;
    /* Per-thread resolved class entries for hooks that resolve a fixed internal
     * class directly (not via the registry): PDO / PDOStatement (hook_pdo.c)
     * and CurlHandle (hook_curl.c). A zend_class_entry* is per-thread under ZTS,
     * so these must not be file-scope statics shared across request threads.
     * curl_ce_resolved gates the one-time CurlHandle lookup for this thread
     * (PDO uses a non-NULL pdo_dbh_ce as its own resolved flag). */
    zend_class_entry *pdo_dbh_ce;
    zend_class_entry *pdo_stmt_ce;
    zend_class_entry *curl_ce;
    zend_bool curl_ce_resolved;
ZEND_END_MODULE_GLOBALS(akari)

ZEND_EXTERN_MODULE_GLOBALS(akari)

#define AKARI_G(v) ZEND_MODULE_GLOBALS_ACCESSOR(akari, v)

/* Compatibility shim: the codebase historically referred to the per-request
 * state as the global `g_state`. It is now per-thread module-global storage.
 * Keeping the spelling avoids churning ~100 references; the lvalue still works
 * (AKARI_G expands to a member access), so `g_state = ...` assignments are
 * unchanged. */
#define g_state AKARI_G(state)

extern zend_module_entry akari_module_entry;
#define phpext_akari_ptr &akari_module_entry

#endif /* PHP_AKARI_H */
