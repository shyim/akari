#include "profiler_internal.h"
#include "hook_registry.h"

/*
 * Twig template rendering instrumentation.
 *
 * Hooks:
 *   Twig\Template::render/display     — full template render
 *   Twig\Template::displayBlock       — individual block render
 *   Twig\TemplateWrapper::render      — wrapper render
 *
 * Extracts template name via $this->getTemplateName() and block name
 * from the first argument of displayBlock(). Records as db.system=twig
 * with the template/block name in db.statement.
 */

/* ── Helper: record twig attr with template name ── */

static void record_twig_attr(profiler_state_t *state, uint32_t span_index,
                               const char *label, size_t label_len)
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
    strcpy(attr->db_system, "twig");

    if (label && label_len > 0) {
        if (label_len >= DB_STATEMENT_MAX) label_len = DB_STATEMENT_MAX - 1;
        memcpy(attr->db_statement, label, label_len);
        attr->db_statement[label_len] = '\0';
        attr->db_statement_len = label_len;
    }

    state->db_attr_count++;
}

/* ── Helper: extract template name from $this->getTemplateName() ── */

static int get_template_name(zend_execute_data *execute_data, char *out, size_t out_size)
{
    zval *this_obj = &execute_data->This;
    if (Z_TYPE_P(this_obj) != IS_OBJECT) return 0;

    zval method_name, retval;
    ZVAL_STRING(&method_name, "getTemplateName");

    int found = 0;
    if (call_user_function(NULL, this_obj, &method_name, &retval, 0, NULL) == SUCCESS) {
        if (Z_TYPE(retval) == IS_STRING && Z_STRLEN(retval) > 0) {
            snprintf(out, out_size, "%s", Z_STRVAL(retval));
            found = 1;
        }
        zval_ptr_dtor(&retval);
    }
    zval_ptr_dtor(&method_name);
    return found;
}

/* ── Template render/display pre-hook ── */

static void *twig_render_pre(profiler_state_t *state, zend_execute_data *execute_data,
                               profiler_span_t *span, uint32_t span_index)
{
    char tpl_name[256] = {0};
    if (get_template_name(execute_data, tpl_name, sizeof(tpl_name))) {
        record_twig_attr(state, span_index, tpl_name, strlen(tpl_name));
        /* Override span name: "twig.render blog/index.html.twig" */
        int n = snprintf(span->name_override, SPAN_NAME_OVERRIDE_MAX,
            "twig.render %s", tpl_name);
        span->name_override_len = profiler_clamp_snprintf_len(n, sizeof(span->name_override));
    } else {
        record_twig_attr(state, span_index, NULL, 0);
    }
    return NULL;
}

/* ── Block display pre-hook ── */

static void *twig_block_pre(profiler_state_t *state, zend_execute_data *execute_data,
                              profiler_span_t *span, uint32_t span_index)
{
    /* displayBlock($name, $context, $blocks, $useBlocks, $templateContext)
     * First arg is the block name string. */
    char label[512] = {0};
    size_t label_len = 0;

    /* Get template name first */
    char tpl_name[256] = {0};
    get_template_name(execute_data, tpl_name, sizeof(tpl_name));

    /* Get block name from first argument */
    const char *block_name = NULL;
    uint32_t num_args = ZEND_CALL_NUM_ARGS(execute_data);
    if (num_args >= 1) {
        zval *block_name_arg = ZEND_CALL_ARG(execute_data, 1);
        if (block_name_arg && Z_TYPE_P(block_name_arg) == IS_STRING) {
            block_name = Z_STRVAL_P(block_name_arg);
            if (tpl_name[0]) {
                int n = snprintf(label, sizeof(label), "%s::%s", tpl_name, block_name);
                label_len = profiler_clamp_snprintf_len(n, sizeof(label));
            } else {
                int n = snprintf(label, sizeof(label), "%s", block_name);
                label_len = profiler_clamp_snprintf_len(n, sizeof(label));
            }
        }
    }

    if (label_len > 0) {
        record_twig_attr(state, span_index, label, label_len);
    } else if (tpl_name[0]) {
        record_twig_attr(state, span_index, tpl_name, strlen(tpl_name));
    } else {
        record_twig_attr(state, span_index, NULL, 0);
    }

    /* Override span name: "twig.block header" or "twig.block template::header" */
    if (block_name) {
        if (tpl_name[0]) {
            int n = snprintf(span->name_override, SPAN_NAME_OVERRIDE_MAX,
                "twig.block %s::%s", tpl_name, block_name);
            span->name_override_len = profiler_clamp_snprintf_len(n, sizeof(span->name_override));
        } else {
            int n = snprintf(span->name_override, SPAN_NAME_OVERRIDE_MAX,
                "twig.block %s", block_name);
            span->name_override_len = profiler_clamp_snprintf_len(n, sizeof(span->name_override));
        }
    }

    return NULL;
}

/* ── Registration ── */

void hook_twig_register(hook_registry_t *reg)
{
    /* Template render/display — full template rendering */
    hook_register_method(reg, "Twig\\Template", "render",
        HOOK_TYPE_USERLAND, SPAN_KIND_INTERNAL, 1, twig_render_pre, NULL);
    hook_register_method(reg, "Twig\\Template", "display",
        HOOK_TYPE_USERLAND, SPAN_KIND_INTERNAL, 1, twig_render_pre, NULL);
    hook_register_method(reg, "Twig\\TemplateWrapper", "render",
        HOOK_TYPE_USERLAND, SPAN_KIND_INTERNAL, 1, twig_render_pre, NULL);

    /* Block rendering — individual block within a template */
    hook_register_method(reg, "Twig\\Template", "displayBlock",
        HOOK_TYPE_USERLAND, SPAN_KIND_INTERNAL, 1, twig_block_pre, NULL);
    hook_register_method(reg, "Twig\\TemplateWrapper", "displayBlock",
        HOOK_TYPE_USERLAND, SPAN_KIND_INTERNAL, 0, twig_block_pre, NULL);
}
