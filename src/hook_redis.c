#include "profiler_internal.h"
#include "hook_registry.h"

/*
 * Redis instrumentation — hooks phpredis (Redis class) and Relay (Relay\Relay class).
 *
 * Uses wildcard class registration: matches any method on Redis/Relay that
 * passes the command filter. This uses 2 registry slots instead of 120+.
 */

/* ── Command detection ── */

/* Commands that take a key as first argument */
static const char *redis_key_commands[] = {
    "get", "set", "del", "exists", "expire", "ttl", "incr", "decr",
    "incrby", "decrby", "append", "strlen", "setex", "setnx", "psetex",
    "mget", "mset", "hget", "hset", "hdel", "hgetall", "hkeys", "hvals",
    "hmget", "hmset", "hexists", "hincrby", "hlen",
    "lpush", "rpush", "lpop", "rpop", "llen", "lrange", "lindex",
    "sadd", "srem", "smembers", "sismember", "scard", "srandmember",
    "zadd", "zrem", "zrange", "zrangebyscore", "zscore", "zcard", "zrank",
    "pfadd", "pfcount",
    "getset", "incrbyfloat", "hincrbyfloat",
    "persist", "expireat", "pexpire", "pexpireat", "pttl", "type",
    "watch", "unwatch",
    "publish", "subscribe",
    "xadd", "xlen", "xrange", "xrevrange", "xread",
    /* Extended commands */
    "blmove", "blmpop", "blpop", "brpop", "brpoplpush",
    "bzmpop", "bzpopmax", "bzpopmin",
    "copy", "dump", "restore",
    "getbit", "getdel", "getex", "getrange",
    "setbit", "setrange",
    "hdel", "hexists", "hget", "hgetall", "hincrby", "hincrbyfloat",
    "hkeys", "hlen", "hmget", "hmset", "hrandfield", "hset", "hsetnx",
    "hstrlen", "hvals", "hscan",
    "lcs", "linsert", "lmove", "lmpop", "lpos",
    "lpushx", "lrem", "lset", "ltrim",
    "pfmerge",
    "psetex", "pttl", "pubsub",
    "rename", "renamenx", "replicaof",
    "rpop", "rpoplpush", "rpush", "rpushx",
    "sdiff", "sdiffstore", "sinter", "sintercard", "sinterstore",
    "sismember", "smove", "sort", "sort_ro",
    "spop", "srem", "sscan", "sunion", "sunionstore",
    "touch", "unlink",
    "xack", "xclaim", "xdel", "xpending", "xrange", "xread", "xreadgroup",
    "xrevrange", "xtrim",
    "zadd", "zcard", "zcount", "zdiff", "zdiffstore",
    "zincrby", "zinter", "zintercard", "zinterstore",
    "zlexcount", "zmpop", "zmscore", "zpopmax", "zpopmin",
    "zrandmember", "zrange", "zrangebylex", "zrangebyscore",
    "zrangestore", "zrank", "zrem", "zremrangebylex",
    "zremrangebyrank", "zremrangebyscore",
    "zrevrange", "zrevrangebylex", "zrevrangebyscore", "zrevrank",
    "zscan", "zscore", "zunion", "zunionstore",
    "geoadd", "geodist", "geohash", "geopos",
    "georadius", "georadius_ro", "georadiusbymember", "georadiusbymember_ro",
    "geosearch", "geosearchstore",
    "bitcount", "bitop", "bitpos",
    NULL
};

/* Commands with no key argument */
static const char *redis_nokey_commands[] = {
    "ping", "echo", "select", "dbsize", "flushdb", "flushall",
    "info", "keys", "scan", "randomkey", "time", "multi", "exec",
    "discard", "pipeline", "auth", "quit", "close",
    "eval", "evalsha", "script",
    /* Extended commands */
    "dbSize", "flushAll", "flushDB",
    "acl", "client", "config", "command", "debug",
    "eval_ro", "evalsha_ro", "fcall", "fcall_ro", "function",
    "lastsave", "save", "bgsave", "bgrewriteaof",
    "failover", "reset", "role", "swapdb", "slowlog",
    "migrate", "move", "object",
    "psubscribe", "punsubscribe",
    "rawcommand",
    "ssubscribe", "sunsubscribe",
    "wait", "waitaof",
    "slaveof",
    "xautoclaim", "xgroup", "xinfo",
    /* RedisCluster-specific */
    "cluster",
    NULL
};

static int is_known_redis_command(const char *method)
{
    for (const char **cmd = redis_key_commands; *cmd; cmd++) {
        if (strcasecmp(method, *cmd) == 0) return 1;
    }
    for (const char **cmd = redis_nokey_commands; *cmd; cmd++) {
        if (strcasecmp(method, *cmd) == 0) return 2; /* 2 = no key */
    }
    return 0;
}

/* Method filter for wildcard registration */
static int redis_method_filter(const char *method_name)
{
    return is_known_redis_command(method_name) > 0;
}

/* ── Registry callback ── */

static void *redis_pre(profiler_state_t *state, zend_execute_data *execute_data,
                        profiler_span_t *span, uint32_t span_index)
{
    (void)span;
    if (!execute_data || !execute_data->func || !execute_data->func->common.function_name) return NULL;

    const char *method = ZSTR_VAL(execute_data->func->common.function_name);

    /* Reuse db_attrs table */
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
    strcpy(attr->db_system, "redis");

    int has_key = (is_known_redis_command(method) == 1);
    uint32_t num_args = ZEND_CALL_NUM_ARGS(execute_data);

    if (has_key && num_args >= 1) {
        zval *key_arg = ZEND_CALL_ARG(execute_data, 1);
        if (key_arg && Z_TYPE_P(key_arg) == IS_STRING) {
            int n = snprintf(attr->db_statement, DB_STATEMENT_MAX,
                "%s %s", method, Z_STRVAL_P(key_arg));
            attr->db_statement_len = (n > 0 && (size_t)n < DB_STATEMENT_MAX) ? (size_t)n : DB_STATEMENT_MAX - 1;
        } else {
            int n = snprintf(attr->db_statement, DB_STATEMENT_MAX, "%s", method);
            attr->db_statement_len = (n > 0 && (size_t)n < DB_STATEMENT_MAX) ? (size_t)n : DB_STATEMENT_MAX - 1;
        }
    } else {
        int n = snprintf(attr->db_statement, DB_STATEMENT_MAX, "%s", method);
        attr->db_statement_len = (n > 0 && (size_t)n < DB_STATEMENT_MAX) ? (size_t)n : DB_STATEMENT_MAX - 1;
    }

    state->db_attr_count++;
    return NULL;
}

/* ── Registration ── */

void hook_redis_register(hook_registry_t *reg)
{
    hook_register_class_wildcard(reg, "Redis",
        HOOK_TYPE_INTERNAL, SPAN_KIND_CLIENT, 1,
        redis_method_filter, redis_pre, NULL);

    hook_register_class_wildcard(reg, "RedisCluster",
        HOOK_TYPE_INTERNAL, SPAN_KIND_CLIENT, 1,
        redis_method_filter, redis_pre, NULL);

    hook_register_class_wildcard(reg, "Relay\\Relay",
        HOOK_TYPE_INTERNAL, SPAN_KIND_CLIENT, 1,
        redis_method_filter, redis_pre, NULL);

    hook_register_class_wildcard(reg, "Relay\\Cluster",
        HOOK_TYPE_INTERNAL, SPAN_KIND_CLIENT, 1,
        redis_method_filter, redis_pre, NULL);
}
