#include "profiler_internal.h"
#include "hook_registry.h"

/*
 * Elasticsearch client instrumentation — userland hooks.
 *
 * Hooks:
 *   - Elastic\Elasticsearch\Client (v8): sendRequest()
 *   - Elasticsearch\Client (v7): performRequest() via Transport
 *   - OpenSearch\Client: sendRequest()
 *
 * These are userland PHP methods, so HOOK_TYPE_USERLAND.
 * Records db.system=elasticsearch.
 */

static void *elasticsearch_pre(profiler_state_t *state, zend_execute_data *execute_data,
                                profiler_span_t *span, uint32_t span_index)
{
    (void)span;
    (void)execute_data;

    if (state->db_attr_count >= state->db_attr_capacity) {
        size_t new_cap = state->db_attr_capacity ? state->db_attr_capacity * 2 : PROFILER_INITIAL_DB_ATTRS;
        profiler_db_attr_t *new_attrs = realloc(state->db_attrs, new_cap * sizeof(profiler_db_attr_t));
        if (!new_attrs) return NULL;
        state->db_attrs = new_attrs;
        state->db_attr_capacity = new_cap;
    }

    profiler_db_attr_t *attr = &state->db_attrs[state->db_attr_count];
    memset(attr, 0, sizeof(profiler_db_attr_t));
    attr->span_index = span_index;
    strcpy(attr->db_system, "elasticsearch");

    state->db_attr_count++;
    return NULL;
}

/* ── Registration ── */

void hook_elasticsearch_register(hook_registry_t *reg)
{
    /* Elasticsearch v8 (elastic/elasticsearch-php 8.x) */
    hook_register_method(reg,
        "Elastic\\Elasticsearch\\Client", "sendRequest",
        HOOK_TYPE_USERLAND, SPAN_KIND_CLIENT, 1,
        elasticsearch_pre, NULL);

    /* Elasticsearch v7 transport layer */
    hook_register_method(reg,
        "Elasticsearch\\Transport", "performRequest",
        HOOK_TYPE_USERLAND, SPAN_KIND_CLIENT, 1,
        elasticsearch_pre, NULL);

    /* OpenSearch client (fork of Elasticsearch) */
    hook_register_method(reg,
        "OpenSearch\\Client", "sendRequest",
        HOOK_TYPE_USERLAND, SPAN_KIND_CLIENT, 1,
        elasticsearch_pre, NULL);
}
