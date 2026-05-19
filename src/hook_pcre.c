#include "profiler_internal.h"
#include "hook_registry.h"

/*
 * PCRE / regex instrumentation — hooks all preg_* functions.
 *
 * Regex is often a hidden bottleneck. Instrumented functions:
 *   - preg_match, preg_match_all
 *   - preg_replace, preg_replace_callback, preg_replace_callback_array
 *   - preg_filter, preg_grep, preg_split
 *
 * Records db.system=regex with the pattern (first arg, truncated) as db.statement.
 * Uses per-hook min_duration threshold because most regex calls are fast.
 */

/* ── Helper ── */

static void record_regex_attr(profiler_state_t *state, uint32_t span_index,
                                const char *pattern, size_t pattern_len)
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
    snprintf(attr->db_system, DB_SYSTEM_MAX, "regex");

    if (pattern && pattern_len > 0) {
        if (pattern_len >= DB_STATEMENT_MAX) pattern_len = DB_STATEMENT_MAX - 1;
        memcpy(attr->db_statement, pattern, pattern_len);
        attr->db_statement[pattern_len] = '\0';
        attr->db_statement_len = pattern_len;
    }

    state->db_attr_count++;
}

/* ── Generic PCRE pre-hook — extracts regex pattern from first argument ── */

static void *pcre_pre(profiler_state_t *state, zend_execute_data *execute_data,
                       profiler_span_t *span, uint32_t span_index)
{
    (void)span;
    uint32_t num_args = ZEND_CALL_NUM_ARGS(execute_data);
    const char *pattern = NULL;
    size_t pattern_len = 0;

    if (num_args >= 1) {
        zval *pattern_arg = ZEND_CALL_ARG(execute_data, 1);
        if (pattern_arg && Z_TYPE_P(pattern_arg) == IS_STRING) {
            pattern = Z_STRVAL_P(pattern_arg);
            pattern_len = Z_STRLEN_P(pattern_arg);
            /* Truncate long patterns for display */
            if (pattern_len > 200) pattern_len = 200;
        }
    }

    record_regex_attr(state, span_index, pattern, pattern_len);
    return NULL;
}

/* ── Registration ──
 *
 * Use min_duration threshold of 1ms to avoid overhead from fast regex calls.
 * Only slow regex executions create spans.
 */

void hook_pcre_register(hook_registry_t *reg)
{
    /* preg_match — match regex, returns count or false */
    hook_register_function_threshold(reg, "preg_match",
        HOOK_TYPE_INTERNAL, SPAN_KIND_INTERNAL, 1.0,
        pcre_pre, NULL);

    /* preg_match_all — match all occurrences */
    hook_register_function_threshold(reg, "preg_match_all",
        HOOK_TYPE_INTERNAL, SPAN_KIND_INTERNAL, 1.0,
        pcre_pre, NULL);

    /* preg_replace — search and replace */
    hook_register_function_threshold(reg, "preg_replace",
        HOOK_TYPE_INTERNAL, SPAN_KIND_INTERNAL, 1.0,
        pcre_pre, NULL);

    /* preg_replace_callback — replace with callback */
    hook_register_function_threshold(reg, "preg_replace_callback",
        HOOK_TYPE_INTERNAL, SPAN_KIND_INTERNAL, 1.0,
        pcre_pre, NULL);

    /* preg_replace_callback_array — multiple pattern/callback pairs */
    hook_register_function_threshold(reg, "preg_replace_callback_array",
        HOOK_TYPE_INTERNAL, SPAN_KIND_INTERNAL, 1.0,
        pcre_pre, NULL);

    /* preg_filter — like preg_replace but returns null for no match */
    hook_register_function_threshold(reg, "preg_filter",
        HOOK_TYPE_INTERNAL, SPAN_KIND_INTERNAL, 1.0,
        pcre_pre, NULL);

    /* preg_grep — filter array by regex */
    hook_register_function_threshold(reg, "preg_grep",
        HOOK_TYPE_INTERNAL, SPAN_KIND_INTERNAL, 1.0,
        pcre_pre, NULL);

    /* preg_split — split string by regex */
    hook_register_function_threshold(reg, "preg_split",
        HOOK_TYPE_INTERNAL, SPAN_KIND_INTERNAL, 1.0,
        pcre_pre, NULL);
}
