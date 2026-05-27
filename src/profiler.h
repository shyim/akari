#ifndef PROFILER_H
#define PROFILER_H

#include <stdint.h>
#include <stddef.h>

/* ── Configuration ── */

#define PROFILER_NAME_MAX         512
#define PROFILER_INITIAL_FRAMES   256
#define PROFILER_INITIAL_SPANS    1024
#define PROFILER_MAX_SPANS        (256 * 1024)
#define PROFILER_MAX_FRAMES        (64 * 1024)
#define PROFILER_MAX_STACK        256
#define PROFILER_BATCH_SIZE       1000
#define PROFILER_FLUSH_THRESHOLD  4096

/* OTel span kinds */
#define SPAN_KIND_INTERNAL  1
#define SPAN_KIND_SERVER    2
#define SPAN_KIND_CLIENT    3
#define SPAN_KIND_PRODUCER  4
#define SPAN_KIND_CONSUMER  5

/* OTel span status */
#define SPAN_STATUS_UNSET  0
#define SPAN_STATUS_OK     1
#define SPAN_STATUS_ERROR  2

/* ── Frame deduplication table ── */

#define PROFILER_FILEPATH_MAX  256
#define PROFILER_FUNCNAME_MAX 256

typedef struct {
    char name[PROFILER_NAME_MAX];         /* display name: Class::method or function */
    size_t name_len;
    char filepath[PROFILER_FILEPATH_MAX]; /* code.filepath */
    size_t filepath_len;
    char function[PROFILER_FUNCNAME_MAX]; /* code.function (without class) */
    size_t function_len;
    char ns[PROFILER_FUNCNAME_MAX];       /* code.namespace (class name) */
    size_t ns_len;
    uint32_t lineno;                      /* code.lineno */
    uint32_t parent_index;                /* index of caller frame, 0 = root */
} profiler_frame_t;

/* ── Span ── */

#define SPAN_NAME_OVERRIDE_MAX 256

typedef struct {
    char trace_id[32];
    char span_id[16];
    char parent_span_id[16];
    uint32_t frame_index;    /* index into frame table */
    uint64_t start_time_ns;
    uint64_t end_time_ns;
    uint32_t depth;
    int has_parent;
    uint8_t kind;            /* SPAN_KIND_* */
    uint8_t status_code;     /* SPAN_STATUS_* */
    char name_override[SPAN_NAME_OVERRIDE_MAX]; /* if set, overrides frame name in export */
    size_t name_override_len;

    /* Observer-only: data passed from begin to end callback */
    void *pre_data;           /* return value from pre_hook */
    int skip_span;            /* pre_hook requested to skip this span */
    int exported;             /* already shipped to forwarder */
    int is_manual;            /* userland-created span finalized at shutdown */
} profiler_span_t;

/* ── Database span attributes (PDO instrumentation) ── */

#define DB_SYSTEM_MAX    32
#define DB_NAME_MAX      128
#define DB_STATEMENT_MAX 2048
#define DB_USER_MAX      64

#define PROFILER_INITIAL_DB_ATTRS 16

typedef struct {
    uint32_t span_index;                /* index into span array */
    char db_system[DB_SYSTEM_MAX];      /* "mysql", "pgsql", "sqlite" */
    char db_name[DB_NAME_MAX];          /* database name */
    char db_user[DB_USER_MAX];          /* username */
    char db_statement[DB_STATEMENT_MAX]; /* SQL query (truncated) */
    size_t db_statement_len;
} profiler_db_attr_t;

/* ── HTTP client span attributes (curl instrumentation) ── */

#define PROFILER_HTTP_URL_MAX 1024

typedef struct {
    uint32_t span_index;
    char url[PROFILER_HTTP_URL_MAX];
    size_t url_len;
    char method[16];                    /* http.request.method */
    char server_address[128];           /* server.address */
    uint16_t server_port;               /* server.port */
    int http_status_code;               /* http.response.status_code */
} profiler_http_attr_t;

/* ── Messaging span attributes (AMQP instrumentation) ── */

typedef struct {
    uint32_t span_index;
    char messaging_system[32];           /* "rabbitmq" */
    char messaging_operation[16];        /* "publish", "receive" */
    char destination_name[256];          /* exchange or queue name */
    char linked_trace_id[32];           /* trace_id from consumed message (for linking) */
    char linked_span_id[16];            /* span_id from consumed message */
    int has_link;                        /* traceparent was found in consumed message */
} profiler_messaging_attr_t;

/* ── Template span attributes (Twig instrumentation) ── */

#define TEMPLATE_ENGINE_MAX 16
#define TEMPLATE_NAME_MAX   256
#define TEMPLATE_BLOCK_MAX  128

#define PROFILER_INITIAL_TEMPLATE_ATTRS 8

typedef struct {
    uint32_t span_index;                   /* index into span array */
    char engine[TEMPLATE_ENGINE_MAX];      /* template.engine, e.g. "twig" */
    char name[TEMPLATE_NAME_MAX];          /* template.name, e.g. "blog/index.html.twig" */
    char block_name[TEMPLATE_BLOCK_MAX];   /* template.block_name (block renders only) */
} profiler_template_attr_t;

/* ── Exception event attributes ── */

#define EXCEPTION_TYPE_MAX    256
#define EXCEPTION_MESSAGE_MAX 512

#define PROFILER_INITIAL_EXCEPTION_EVENTS 4

typedef struct {
    uint32_t span_index;                     /* index into span array */
    uint64_t timestamp_ns;                   /* event timestamp */
    char exception_type[EXCEPTION_TYPE_MAX]; /* exception class name */
    char exception_message[EXCEPTION_MESSAGE_MAX]; /* exception message */
    /* Caught-detection: events recorded by the engine throw hook start as
     * "pending". Once the throwing span returns with no exception still live,
     * the exception was caught by application code and the event is dropped;
     * if it unwinds past the outermost frame while live, it is uncaught and
     * promoted to an error. Events from explicit framework error handlers /
     * userland logException() are not pending — they are kept unconditionally. */
    uint8_t pending;                         /* 1 = awaiting caught/escaped decision */
    void *exception_obj;                     /* zend_object* thrown (engine hook only) */
} profiler_exception_event_t;

/* ── Root span (HTTP request) ── */

#define ROOT_ATTR_MAX 256

typedef struct {
    char span_id[16];
    char parent_span_id[16];   /* from traceparent header */
    int has_parent;            /* traceparent was present */
    uint64_t start_time_ns;
    uint64_t end_time_ns;

    /* HTTP attributes */
    char http_method[16];
    char url_path[ROOT_ATTR_MAX];
    char url_scheme[8];
    char server_address[128];
    int server_port;
    int http_status_code;

    /* Framework-detected attributes */
    char http_route[ROOT_ATTR_MAX];         /* route name (e.g. "app_blog_index") */
    char http_controller[ROOT_ATTR_MAX];    /* controller (e.g. "App\\Controller\\BlogController::index") */

    /* Span name: "METHOD controller" or "METHOD route" or "METHOD /path" */
    char name[ROOT_ATTR_MAX];
    size_t name_len;

    uint8_t status_code;       /* SPAN_STATUS_* */
    char status_message[256];

    int is_cli;                /* CLI SAPI = no root HTTP span */
    int active;

    /* Runtime environment annotations */
    char php_version[32];
    char php_sapi[32];
    int opcache_enabled;
    long opcache_memory_mb;
    long opcache_max_files;
    long opcache_interned_mb;
    char date_timezone[64];
    long max_execution_time;
    long memory_limit_mb;
    long realpath_cache_size;
    int display_errors;
    int zend_assertions;
    long peak_memory_bytes;    /* filled at finalize */
} profiler_root_span_t;

/* ── Flush callback type ── */

struct profiler_state_s;
typedef void (*profiler_flush_fn)(struct profiler_state_s *state, void *user_data);

/* ── Per-request trace state ── */

typedef struct profiler_state_s {
    char trace_id[32];

    /* Root span */
    profiler_root_span_t root;

    /* Frame dedup table */
    profiler_frame_t *frames;
    size_t frame_count;
    size_t frame_capacity;

    /* Span buffer */
    profiler_span_t *spans;
    size_t span_count;
    size_t span_capacity;

    /* Database attributes (for PDO spans) */
    profiler_db_attr_t *db_attrs;
    size_t db_attr_count;
    size_t db_attr_capacity;

    /* HTTP client attributes (for curl spans) */
    profiler_http_attr_t *http_attrs;
    size_t http_attr_count;
    size_t http_attr_capacity;

    /* Messaging attributes (for AMQP spans) */
    profiler_messaging_attr_t *msg_attrs;
    size_t msg_attr_count;
    size_t msg_attr_capacity;

    /* Template attributes (for Twig spans) */
    profiler_template_attr_t *template_attrs;
    size_t template_attr_count;
    size_t template_attr_capacity;

    /* Exception events (for error handler spans) */
    profiler_exception_event_t *exception_events;
    size_t exception_event_count;
    size_t exception_event_capacity;

    /* Escaped (uncaught) exception tracking, independent of hooked spans.
     * Set when an exception unwinds past the outermost frame while still live
     * (observer_fcall_end at stack_depth 0). Lets finalize_root_span mark the
     * root ERROR even when no hooked span was on the stack and even after the
     * engine has cleared EG(exception) by request shutdown. */
    int root_exception_escaped;
    char root_exception_message[EXCEPTION_MESSAGE_MAX];

    /* Call stack (indices into spans for open spans) */
    size_t stack[PROFILER_MAX_STACK];
    size_t stack_depth;
    int stack_overflow_count;           /* pushes dropped when stack was full */

    int active;
    uint32_t max_depth;
    uint64_t min_duration_ns;   /* spans shorter than this are dropped post-execution */
    uint64_t rng_state;
    int overflow_warned;

    /* Flush-on-threshold */
    size_t flush_threshold;
    profiler_flush_fn flush_fn;
    void *flush_user_data;

    /* Userland API: custom tags */
    char tags[16][128];
    int tag_count;

    /* Userland API: custom transaction name override */
    char custom_transaction_name[ROOT_ATTR_MAX];
    int has_custom_transaction;

    /* Userland API: service name override (set by setServiceName()) */
    char service_name_override[ROOT_ATTR_MAX];

    /* Userland API: manual span tracking */
    int manual_spans[32];  /* span indices that are manually created */
    int manual_span_count;
} profiler_state_t;

/* ── API ── */

void profiler_minit(void);
void profiler_mshutdown(void);
void profiler_rinit(uint32_t max_depth, double min_duration_ms);
void profiler_rshutdown(void);

void profiler_set_flush_callback(profiler_flush_fn fn, void *user_data);
void profiler_set_flush_threshold(size_t threshold);
profiler_state_t *profiler_get_state(void);

/* Helpers for export */
void profiler_generate_hex_id(profiler_state_t *state, char *buf, size_t len);
void profiler_resolve_pdo_classes(void);
const profiler_frame_t *profiler_get_frame(profiler_state_t *state, uint32_t frame_index);
int profiler_current_span_index(profiler_state_t *state, uint32_t *span_index);
const profiler_db_attr_t *profiler_get_db_attr(profiler_state_t *state, uint32_t span_index);
const profiler_http_attr_t *profiler_get_http_attr(profiler_state_t *state, uint32_t span_index);
const profiler_messaging_attr_t *profiler_get_messaging_attr(profiler_state_t *state, uint32_t span_index);
const profiler_template_attr_t *profiler_get_template_attr(profiler_state_t *state, uint32_t span_index);
const profiler_exception_event_t *profiler_get_exception_event(profiler_state_t *state, uint32_t span_index);
int profiler_span_has_pending_exception(profiler_state_t *state, uint32_t span_index);

#endif /* PROFILER_H */
