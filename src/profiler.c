#include "profiler_internal.h"
#include "hook_registry.h"

/* ── State ── */

profiler_state_t *g_state = NULL;

/* ── Registry initialization ── */

static int registry_initialized = 0;

static void init_hook_registry(void)
{
    if (registry_initialized) return;
    registry_initialized = 1;

    hook_registry_init(&g_hook_registry);

    /* Register all hook modules */
    hook_pdo_register(&g_hook_registry);
    hook_curl_register(&g_hook_registry);
    hook_redis_register(&g_hook_registry);
    hook_amqp_register(&g_hook_registry);
    hook_mysqli_register(&g_hook_registry);
    hook_memcached_register(&g_hook_registry);
    hook_io_register(&g_hook_registry);
    hook_framework_register(&g_hook_registry);
    hook_twig_register(&g_hook_registry);
    hook_doctrine_register(&g_hook_registry);
    hook_symfony_register(&g_hook_registry);
    hook_elasticsearch_register(&g_hook_registry);
    hook_predis_register(&g_hook_registry);
    hook_laravel_register(&g_hook_registry);
    hook_shopware_register(&g_hook_registry);
    hook_error_register(&g_hook_registry);
    hook_oci8_register(&g_hook_registry);
    hook_grpc_register(&g_hook_registry);
    hook_rdkafka_register(&g_hook_registry);
    hook_graphql_register(&g_hook_registry);
    hook_soap_register(&g_hook_registry);
    hook_pheanstalk_register(&g_hook_registry);
    hook_php_streams_register(&g_hook_registry);
    hook_pcre_register(&g_hook_registry);
}

/* ── Lifecycle ── */

void profiler_minit(void)
{
    init_hook_registry();
    observer_register();
}

void profiler_mshutdown(void)
{
    if (g_state) {
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
    hook_registry_destroy(&g_hook_registry);
    registry_initialized = 0;
}

void profiler_rinit(uint32_t max_depth, double min_duration_ms)
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
    g_state->service_name_override[0] = '\0';
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

    /* Reset resolved class entries in the registry (invalidated between requests) */
    hook_registry_reset(&g_hook_registry);

    /* Initialize root HTTP span (may override trace_id from traceparent) */
    init_root_span(g_state);

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

    /* Reset userland API state */
    g_state->tag_count = 0;
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
