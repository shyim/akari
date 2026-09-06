#include "profiler_internal.h"
#include "hook_registry.h"

/* Stable lowercase layer names, indexed by AKARI_LAYER_*. Used to build export
 * keys like "akari.layer.db.duration_ms". Order must match the AKARI_LAYER_*
 * defines in profiler.h. */
const char *const profiler_layer_names[AKARI_LAYER_MAX] = {
    "app",        /* AKARI_LAYER_APP */
    "db",         /* AKARI_LAYER_DB */
    "cache",      /* AKARI_LAYER_CACHE */
    "http",       /* AKARI_LAYER_HTTP */
    "template",   /* AKARI_LAYER_TEMPLATE */
    "messaging",  /* AKARI_LAYER_MESSAGING */
    "search",     /* AKARI_LAYER_SEARCH */
    "compile",    /* AKARI_LAYER_COMPILE */
    "gc",         /* AKARI_LAYER_GC */
    "io",         /* AKARI_LAYER_IO */
};

/* ── State ──
 *
 * The per-request profiler state lives in module globals as AKARI_G(state),
 * aliased to `g_state` (see php_akari.h). Under ZTS that makes it per-thread,
 * so concurrent requests on a threaded SAPI no longer share one buffer. There
 * is no file-scope definition here anymore. */

/* ── Registry initialization ── */

static int registry_initialized = 0;

static void init_hook_registry(void)
{
    if (registry_initialized) return;
    registry_initialized = 1;

    hook_registry_init(&g_hook_registry);

    /* Register all hook modules. Each module's hooks are stamped with a layer
     * (via reg->default_layer) so completed spans can be attributed to a layer
     * for the per-request "where did the time go" summary. REG(layer, fn) sets
     * the layer, registers the module, then restores APP as the default so an
     * unstamped module falls through to APP. */
#define REG(layer, fn) do { \
        g_hook_registry.default_layer = (layer); \
        fn(&g_hook_registry); \
        g_hook_registry.default_layer = AKARI_LAYER_APP; \
    } while (0)

    REG(AKARI_LAYER_DB,        hook_pdo_register);
    REG(AKARI_LAYER_DB,        hook_sqlite3_register);
    REG(AKARI_LAYER_HTTP,      hook_curl_register);
    REG(AKARI_LAYER_CACHE,     hook_redis_register);
    REG(AKARI_LAYER_MESSAGING, hook_amqp_register);
    REG(AKARI_LAYER_DB,        hook_mysqli_register);
    REG(AKARI_LAYER_CACHE,     hook_memcached_register);
    REG(AKARI_LAYER_IO,        hook_io_register);
    REG(AKARI_LAYER_APP,       hook_framework_register);
    REG(AKARI_LAYER_TEMPLATE,  hook_twig_register);
    REG(AKARI_LAYER_DB,        hook_doctrine_register);
    REG(AKARI_LAYER_APP,       hook_symfony_register);
    REG(AKARI_LAYER_SEARCH,    hook_elasticsearch_register);
    REG(AKARI_LAYER_CACHE,     hook_predis_register);
    REG(AKARI_LAYER_APP,       hook_laravel_register);
    REG(AKARI_LAYER_APP,       hook_shopware_register);
    REG(AKARI_LAYER_APP,       hook_error_register);
    REG(AKARI_LAYER_DB,        hook_oci8_register);
    REG(AKARI_LAYER_HTTP,      hook_grpc_register);
    REG(AKARI_LAYER_MESSAGING, hook_rdkafka_register);
    REG(AKARI_LAYER_APP,       hook_graphql_register);
    REG(AKARI_LAYER_HTTP,      hook_soap_register);
    REG(AKARI_LAYER_MESSAGING, hook_pheanstalk_register);
    REG(AKARI_LAYER_HTTP,      hook_php_streams_register);
    REG(AKARI_LAYER_APP,       hook_pcre_register);

#undef REG
}

/* ── Lifecycle ── */

void profiler_minit(void)
{
    init_hook_registry();
    observer_register();
}

/* Free the calling thread's profiler state and its buffers. The state lives in
 * module globals (per-thread under ZTS) and outlives individual requests
 * (profiler_rshutdown only resets it), so it must be freed at thread teardown —
 * PHP_GSHUTDOWN — for every thread, not just MSHUTDOWN on the main thread.
 * Idempotent; safe to call when no state was ever allocated. */
void profiler_free_state(void)
{
    if (g_state) {
        hook_attribute_cache_free(g_state);
        free(g_state->frames);
        free(g_state->spans);
        free(g_state->db_attrs);
        free(g_state->http_attrs);
        free(g_state->msg_attrs);
        free(g_state->template_attrs);
        free(g_state->exception_events);
        free(g_state->log_records);
        free(g_state);
        g_state = NULL;
    }
}

void profiler_mshutdown(void)
{
#ifndef ZTS
    /* NTS: there is no PHP_GSHUTDOWN per-thread teardown, so free the single
     * process state here. Under ZTS the state is per-thread and freed in
     * PHP_GSHUTDOWN for every thread (including the main one); doing it here too
     * would risk touching TSRM storage during module teardown. */
    profiler_free_state();
#endif
    /* The hook registry is a single process-global built once at MINIT — tear
     * it down once, here at module shutdown, not per thread. */
    hook_registry_destroy(&g_hook_registry);
    registry_initialized = 0;
}

/* Decide, once per request, whether to sample it for full child-span collection.
 * An inbound traceparent's sampled flag wins (distributed traces must sample
 * consistently end to end); otherwise roll the configured rate. A rate >= 1
 * always samples and <= 0 never does, without consuming RNG. */
static int sample_decide(profiler_state_t *state, double sample_rate)
{
    if (state->root.has_parent) {
        return state->root.parent_sampled;
    }
    if (sample_rate >= 1.0) return 1;
    if (sample_rate <= 0.0) return 0;
    /* Uniform [0,1) from the top 53 bits of the xorshift stream, compared to the
     * rate. Uses the same PRNG as ID generation (already seeded this request). */
    uint64_t r = profiler_random_u64(state) >> 11;   /* 53-bit mantissa */
    double u = (double)r / (double)(1ULL << 53);
    return u < sample_rate;
}

void profiler_rinit(uint32_t max_depth, double min_duration_ms, double sample_rate)
{
    if (!g_state) {
        g_state = calloc(1, sizeof(profiler_state_t));
        if (!g_state) return;

        g_state->frames = malloc(PROFILER_INITIAL_FRAMES * sizeof(profiler_frame_t));
        if (!g_state->frames) { free(g_state); g_state = NULL; return; }
        g_state->frame_capacity = PROFILER_INITIAL_FRAMES;

        g_state->spans = malloc(PROFILER_INITIAL_SPANS * sizeof(profiler_span_t));
        if (!g_state->spans) { free(g_state->frames); free(g_state); g_state = NULL; return; }
        g_state->span_capacity = PROFILER_INITIAL_SPANS;
    }

    /* Reset for new request */
    g_state->frame_count = 0;
    g_state->span_count = 0;
    g_state->db_attr_count = 0;
    g_state->http_attr_count = 0;
    g_state->msg_attr_count = 0;
    g_state->template_attr_count = 0;
    g_state->exception_event_count = 0;
    g_state->log_record_count = 0;
    g_state->log_records_sent = 0;
    g_state->log_overflow_warned = 0;
    g_state->root_exception_escaped = 0;
    g_state->root_exception_message[0] = '\0';
    g_state->stack_depth = 0;
    g_state->stack_overflow_count = 0;
    g_state->event_dispatch_depth = 0;
    g_state->service_name_override[0] = '\0';
    g_state->tag_count = 0;
    memset(&g_state->layers, 0, sizeof(g_state->layers));
    g_state->layer_stack_depth = 0;
    g_state->layer_stack_overflow = 0;
    g_state->max_depth = (max_depth > PROFILER_MAX_STACK) ? PROFILER_MAX_STACK
                       : (max_depth < 1) ? 1 : (uint32_t)max_depth;
    /* Clamp min_duration: 0 to 10 seconds */
    if (min_duration_ms < 0) min_duration_ms = 0;
    if (min_duration_ms > 10000.0) min_duration_ms = 10000.0;
    g_state->min_duration_ns = (uint64_t)(min_duration_ms * 1000000.0);
    g_state->rng_state = 0;
    g_state->overflow_warned = 0;
    g_state->flush_threshold = PROFILER_FLUSH_THRESHOLD;
    profiler_generate_hex_id(g_state, g_state->trace_id, 32);

    /* Reset the per-thread resolved-class-entry cache (CEs are per-request and
     * per-thread). Allocate it lazily on first use for this thread. */
    if (!AKARI_G(ce_cache)) {
        AKARI_G(ce_cache) = calloc(1, sizeof(hook_ce_cache_t));
    }
    hook_ce_cache_reset(AKARI_G(ce_cache));
    hook_registry_reset(&g_hook_registry);

    /* Initialize root HTTP span (may override trace_id from traceparent, and
     * records any inbound sampled flag that the decision below must honor). */
    init_root_span(g_state);

    /* Trace sampling (OTEL_TRACES_SAMPLER): evaluated once per request, after
     * the root span has parsed the incoming trace context (traceparent header /
     * TRACEPARENT env). An unsampled request leaves the profiler inactive — the
     * observer installs no handlers and nothing is buffered or exported. */
    if (!akari_sampling_decide(g_state)) {
        g_state->root.active = 0;
        g_state->active = 0;
        return;
    }

    /* Head sampling decision (akari.sample_rate), made once for requests that
     * survived the trace sampler above. When sampled, the request builds the
     * full child-span tree. When NOT sampled ("keep-frame"), the observer still
     * runs to accumulate the per-layer summary, but no child spans are created
     * or kept — only the root span + layer breakdown are exported. */
    g_state->sampled = sample_decide(g_state, sample_rate);

    /* Initialize curl header tracking for trace propagation */
    curl_propagation_rinit();

    g_state->active = 1;
}

void profiler_rshutdown_finalize(void)
{
    if (!g_state || !g_state->active) return;
    uint64_t manual_end_time_ns = 0;
    for (size_t i = 0; i < g_state->span_count; i++) {
        profiler_span_t *span = &g_state->spans[i];
        if (span->is_manual && span->end_time_ns == 0) {
            if (manual_end_time_ns == 0) {
                manual_end_time_ns = realtime_ns();
            }
            span->end_time_ns = manual_end_time_ns;
        }
    }

    /* Final exception resolution. During normal unwinding observer_fcall_end
     * already promoted escaped exceptions and dropped caught ones. By now the
     * engine has cleared EG(exception), so any event still pending could not be
     * confirmed as escaped — mark_escaped drops it (no live exception to match)
     * rather than reporting a false error. Must run before finalize_root_span
     * so root error status is consistent. */
    profiler_mark_escaped_exceptions(g_state);

    /* Finalize root span: sets end_time_ns, HTTP status, peak memory, error status.
     * Must happen BEFORE export so the root span is included in the trace. */
    finalize_root_span(g_state);
}

void profiler_rshutdown(void)
{
    if (!g_state || !g_state->active) return;

    /* Safe to call again if caller already finalized. */
    profiler_rshutdown_finalize();
    g_state->active = 0;

    /* Clean up curl header tracking */
    curl_propagation_rshutdown();

    /* Clean up SQLite3 prepared-statement SQL tracking */
    sqlite3_rshutdown();

    /* Free per-request attribute arrays to prevent memory bloat */
    free(g_state->db_attrs);  g_state->db_attrs = NULL;  g_state->db_attr_capacity = 0;
    free(g_state->http_attrs); g_state->http_attrs = NULL; g_state->http_attr_capacity = 0;
    free(g_state->msg_attrs);  g_state->msg_attrs = NULL;  g_state->msg_attr_capacity = 0;
    free(g_state->template_attrs); g_state->template_attrs = NULL; g_state->template_attr_capacity = 0;
    free(g_state->exception_events); g_state->exception_events = NULL; g_state->exception_event_capacity = 0;
    free(g_state->log_records); g_state->log_records = NULL; g_state->log_record_capacity = 0;
    g_state->log_record_count = 0;
    g_state->log_records_sent = 0;
    g_state->log_overflow_warned = 0;

    /* Free #[Akari\Span] attribute lookup cache */
    hook_attribute_cache_free(g_state);

    /* Reset userland API state */
    g_state->manual_span_count = 0;
    g_state->has_custom_transaction = 0;
    g_state->service_name_override[0] = '\0';

    /* Final flush of remaining spans is done by the caller (php_profiler_otel.c) */
}

void profiler_set_flush_callback(profiler_flush_fn fn, void *user_data)
{
    if (g_state) {
        g_state->flush_fn = fn;
        g_state->flush_user_data = user_data;
    }
}

void profiler_set_flush_threshold(size_t threshold)
{
    /* A threshold of 0 would flush on every completed span; clamp to 1. */
    if (g_state) {
        g_state->flush_threshold = threshold ? threshold : 1;
    }
}

profiler_state_t *profiler_get_state(void)
{
    return g_state;
}
