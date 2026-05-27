#include "profiler_internal.h"
#include "hook_registry.h"

/*
 * Twig template rendering instrumentation.
 *
 * Hooks:
 *   Twig\Template::render/display     — full template render
 *   Twig\Template::displayBlock       — individual block render
 *
 * Twig\TemplateWrapper (returned by Environment::load()) is intentionally not
 * hooked: its render()/displayBlock() delegate straight to the inner compiled
 * Twig\Template, which we already instrument with the correct template name.
 * TemplateWrapper::getTemplateName() is a delegating call rather than a compiled
 * constant return, so hooking it would only add a redundant span we could not
 * name without a userland call.
 *
 * Extracts template name via $this->getTemplateName() and block name
 * from the first argument of displayBlock(). Records as template.engine=twig
 * with template.name and (for block renders) template.block_name.
 */

/* ── Helper: record twig attr with template + block name ── */

static void record_twig_attr(profiler_state_t *state, uint32_t span_index,
                               const char *tpl_name, const char *block_name)
{
    if (state->template_attr_count >= state->template_attr_capacity) {
        size_t new_cap = state->template_attr_capacity
            ? state->template_attr_capacity * 2
            : PROFILER_INITIAL_TEMPLATE_ATTRS;
        profiler_template_attr_t *new_attrs = realloc(state->template_attrs,
            new_cap * sizeof(profiler_template_attr_t));
        if (!new_attrs) return;
        state->template_attrs = new_attrs;
        state->template_attr_capacity = new_cap;
    }

    profiler_template_attr_t *attr = &state->template_attrs[state->template_attr_count];
    memset(attr, 0, sizeof(profiler_template_attr_t));
    attr->span_index = span_index;
    snprintf(attr->engine, sizeof(attr->engine), "twig");

    if (tpl_name && tpl_name[0]) {
        snprintf(attr->name, sizeof(attr->name), "%s", tpl_name);
    }
    if (block_name && block_name[0]) {
        snprintf(attr->block_name, sizeof(attr->block_name), "%s", block_name);
    }

    state->template_attr_count++;
}

/* ── Helper: extract template name from a compiled template class ──
 *
 * Twig compiles each template to a PHP class whose getTemplateName() is a
 * single unconditional `return "the/template/name.twig";`. Rather than invoke
 * that method on every render/displayBlock (a userland call in a hot path),
 * read the literal string straight out of the compiled opcodes. This is the
 * same trick Blackfire uses.
 *
 * A single `return 'name';` compiles to one ZEND_RETURN of an IS_CONST string
 * (plus a trailing implicit `RETURN null` the compiler always appends, and a
 * VERIFY_RETURN_TYPE for a typed `: string` signature — neither carries a
 * constant string operand). A branchy override such as
 *   if ($c) return 'a.twig'; return 'b.twig';
 * compiles to two constant-string returns, so the name is conditional and we
 * cannot resolve it statically.
 *
 * So: accept only when there is exactly one ZEND_RETURN with an IS_CONST string
 * operand. Zero means the name is computed; more than one means it is
 * conditional. In either case we refuse to guess and report "<unknown>" rather
 * than invoke the method — calling it on every render is too costly for the hot
 * path and is unnecessary for real Twig, which always emits a single constant
 * return. */

#define TWIG_UNKNOWN_TEMPLATE "<unknown>"

static int scan_template_name_const(zend_class_entry *ce, char *out, size_t out_size)
{
    if (!ce) return 0;

    zend_function *func = zend_hash_str_find_ptr(&ce->function_table,
                                                 "gettemplatename", sizeof("gettemplatename") - 1);
    if (!func || func->type != ZEND_USER_FUNCTION) return 0;

    zend_op_array *op_array = &func->op_array;
    if (!op_array->opcodes || op_array->last == 0) return 0;

    /* Find the unique constant-string return. The implicit trailing
     * `RETURN null` has an IS_UNUSED/IS_CONST-null operand and is skipped by the
     * IS_CONST string check; a second matching return means a conditional name. */
    zval *found = NULL;
    for (uint32_t i = 0; i < op_array->last; i++) {
        zend_op *op = &op_array->opcodes[i];
        if (op->opcode != ZEND_RETURN || op->op1_type != IS_CONST) continue;

        zval *zv = RT_CONSTANT(op, op->op1);
        if (!zv || Z_TYPE_P(zv) != IS_STRING || Z_STRLEN_P(zv) == 0) continue;

        if (found) return 0;  /* second constant return — conditional name */
        found = zv;
    }
    if (!found) return 0;

    snprintf(out, out_size, "%s", Z_STRVAL_P(found));
    return 1;
}

static int get_template_name(zend_execute_data *execute_data, char *out, size_t out_size)
{
    zval *this_obj = &execute_data->This;
    if (Z_TYPE_P(this_obj) != IS_OBJECT) return 0;

    if (scan_template_name_const(Z_OBJCE_P(this_obj), out, out_size)) return 1;

    /* getTemplateName() is not a single constant return — we cannot resolve the
     * name statically and will not pay a userland call per render. Report it as
     * unknown rather than risk a wrong (guessed) template name. */
    snprintf(out, out_size, "%s", TWIG_UNKNOWN_TEMPLATE);
    return 1;
}

/* ── Template render/display pre-hook ── */

static void *twig_render_pre(profiler_state_t *state, zend_execute_data *execute_data,
                               profiler_span_t *span, uint32_t span_index)
{
    char tpl_name[256] = {0};
    if (get_template_name(execute_data, tpl_name, sizeof(tpl_name))) {
        record_twig_attr(state, span_index, tpl_name, NULL);
        /* Override span name: "twig.render blog/index.html.twig" */
        int n = snprintf(span->name_override, SPAN_NAME_OVERRIDE_MAX,
            "twig.render %s", tpl_name);
        span->name_override_len = profiler_clamp_snprintf_len(n, sizeof(span->name_override));
    } else {
        record_twig_attr(state, span_index, NULL, NULL);
    }
    return NULL;
}

/* ── Block display pre-hook ── */

static void *twig_block_pre(profiler_state_t *state, zend_execute_data *execute_data,
                              profiler_span_t *span, uint32_t span_index)
{
    /* displayBlock($name, $context, $blocks, $useBlocks, $templateContext)
     * First arg is the block name string. */

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
        }
    }

    record_twig_attr(state, span_index, tpl_name[0] ? tpl_name : NULL, block_name);

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

    /* Block rendering — individual block within a template */
    hook_register_method(reg, "Twig\\Template", "displayBlock",
        HOOK_TYPE_USERLAND, SPAN_KIND_INTERNAL, 1, twig_block_pre, NULL);
}
