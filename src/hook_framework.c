#include "profiler_internal.h"
#include "hook_registry.h"

/*
 * Framework integration — detects known framework entry points and
 * enriches the root span with framework-specific route/controller names.
 *
 * Symfony: HttpKernelInterface::handle()
 *   → extracts _controller and _route from request attributes
 *   → sets root span name to "METHOD Controller::action"
 *
 * Laravel: Router::dispatch()/dispatchToRoute()
 *   → extracts currentRouteName() and currentRouteAction()
 *   → sets root span name to "METHOD Controller@action"
 */

/* ── Helper: read a string from $request->attributes->get($key) ── */

static int read_request_attribute(zval *request, const char *key,
                                    char *out, size_t out_size)
{
    if (!request || Z_TYPE_P(request) != IS_OBJECT) return 0;

    zval attributes_prop;
    zval *attributes = zend_read_property(Z_OBJCE_P(request), Z_OBJ_P(request),
                                           "attributes", 10, 1, &attributes_prop);
    if (!attributes || Z_TYPE_P(attributes) != IS_OBJECT) return 0;

    zval get_method, key_zval, result;
    ZVAL_STRING(&get_method, "get");
    ZVAL_STRING(&key_zval, key);
    zval params[1];
    params[0] = key_zval;

    int found = 0;
    if (call_user_function(NULL, attributes, &get_method, &result, 1, params) == SUCCESS) {
        if (Z_TYPE(result) == IS_STRING && Z_STRLEN(result) > 0) {
            snprintf(out, out_size, "%s", Z_STRVAL(result));
            found = 1;
        }
        zval_ptr_dtor(&result);
    }
    zval_ptr_dtor(&get_method);
    zval_ptr_dtor(&key_zval);

    return found;
}

/* ── Symfony: post-hook extracts controller + route ── */

static void symfony_kernel_post(profiler_state_t *state, zend_execute_data *execute_data,
                                 zval *return_value, profiler_span_t *span,
                                 uint32_t span_index, void *pre_data)
{
    (void)span;
    (void)span_index;
    (void)pre_data;
    (void)return_value;

    if (!state->root.active && state->root.end_time_ns == 0) return;

    uint32_t num_args = ZEND_CALL_NUM_ARGS(execute_data);
    if (num_args < 1) return;

    zval *request = ZEND_CALL_ARG(execute_data, 1);

    /* Extract _controller (e.g. "App\Controller\BlogController::index") */
    read_request_attribute(request, "_controller",
                            state->root.http_controller, sizeof(state->root.http_controller));

    /* Extract _route (e.g. "app_blog_index") */
    read_request_attribute(request, "_route",
                            state->root.http_route, sizeof(state->root.http_route));

    /* Update root span name — prefer controller, fall back to route */
    if (state->root.http_controller[0]) {
        int n = snprintf(state->root.name, sizeof(state->root.name),
            "%s %s", state->root.http_method, state->root.http_controller);
        state->root.name_len = profiler_clamp_snprintf_len(n, sizeof(state->root.name));
    } else if (state->root.http_route[0]) {
        int n = snprintf(state->root.name, sizeof(state->root.name),
            "%s %s", state->root.http_method, state->root.http_route);
        state->root.name_len = profiler_clamp_snprintf_len(n, sizeof(state->root.name));
    }
}

/* ── Laravel: post-hook extracts route name + controller action ── */

static void laravel_router_post(profiler_state_t *state, zend_execute_data *execute_data,
                                 zval *return_value, profiler_span_t *span,
                                 uint32_t span_index, void *pre_data)
{
    (void)return_value;
    (void)span;
    (void)span_index;
    (void)pre_data;

    if (!state->root.active && state->root.end_time_ns == 0) return;

    zval *this_obj = &execute_data->This;
    if (Z_TYPE_P(this_obj) != IS_OBJECT) return;

    /* Try currentRouteAction() first — gives "App\Http\Controllers\FooController@bar" */
    zval method_name, retval;
    ZVAL_STRING(&method_name, "currentRouteAction");
    if (call_user_function(NULL, this_obj, &method_name, &retval, 0, NULL) == SUCCESS) {
        if (Z_TYPE(retval) == IS_STRING && Z_STRLEN(retval) > 0) {
            snprintf(state->root.http_controller, sizeof(state->root.http_controller),
                     "%s", Z_STRVAL(retval));
        }
        zval_ptr_dtor(&retval);
    }
    zval_ptr_dtor(&method_name);

    /* Also get route name */
    ZVAL_STRING(&method_name, "currentRouteName");
    if (call_user_function(NULL, this_obj, &method_name, &retval, 0, NULL) == SUCCESS) {
        if (Z_TYPE(retval) == IS_STRING && Z_STRLEN(retval) > 0) {
            snprintf(state->root.http_route, sizeof(state->root.http_route),
                     "%s", Z_STRVAL(retval));
        }
        zval_ptr_dtor(&retval);
    }
    zval_ptr_dtor(&method_name);

    /* Update root span name — prefer controller, fall back to route */
    if (state->root.http_controller[0]) {
        int n = snprintf(state->root.name, sizeof(state->root.name),
            "%s %s", state->root.http_method, state->root.http_controller);
        state->root.name_len = profiler_clamp_snprintf_len(n, sizeof(state->root.name));
    } else if (state->root.http_route[0]) {
        int n = snprintf(state->root.name, sizeof(state->root.name),
            "%s %s", state->root.http_method, state->root.http_route);
        state->root.name_len = profiler_clamp_snprintf_len(n, sizeof(state->root.name));
    }
}

/* ── Registration ── */

void hook_framework_register(hook_registry_t *reg)
{
    /* Symfony: HttpKernelInterface::handle() */
    hook_register_method(reg,
        "Symfony\\Component\\HttpKernel\\HttpKernelInterface", "handle",
        HOOK_TYPE_USERLAND, SPAN_KIND_INTERNAL, 1,
        NULL, symfony_kernel_post);

    /* Laravel: Router::dispatch() and Router::dispatchToRoute() */
    hook_register_method(reg,
        "Illuminate\\Routing\\Router", "dispatch",
        HOOK_TYPE_USERLAND, SPAN_KIND_INTERNAL, 1,
        NULL, laravel_router_post);
    hook_register_method(reg,
        "Illuminate\\Routing\\Router", "dispatchToRoute",
        HOOK_TYPE_USERLAND, SPAN_KIND_INTERNAL, 1,
        NULL, laravel_router_post);
}
