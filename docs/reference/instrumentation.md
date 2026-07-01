---
icon: lucide/radar
---

# Instrumentation coverage

Akari ships instrumentation hooks for the most common databases, caches, HTTP
clients, messaging systems, and PHP frameworks. Each hook produces standard
OpenTelemetry spans with semantic-convention attributes.

## Database & cache

| System | What's traced | Attributes |
|--------|--------------|------------|
| **PDO** | `query`, `exec`, `prepare`, `beginTransaction`, `commit`, `rollback` | `db.system`, `db.name`, `db.statement`, `db.user` |
| **SQLite3** | `query`, `querySingle`, `exec`, `prepare`, `SQLite3Stmt::execute` (SQL recovered from `prepare`) | `db.system=sqlite`, `db.statement` |
| **MySQLi** | OOP + procedural, `connect`, `prepare`, `execute`, transactions | `db.system=mysql`, SQL statement |
| **OCI8 / Oracle** | `oci_parse`, `oci_execute`, `oci_connect`, `oci_commit`, `oci_rollback` | `db.system=oracle` |
| **Redis / RedisCluster** | 150+ commands with key extraction (phpredis + Relay) | `db.system=redis`, `db.statement=GET key` |
| **Predis** | `Client::executeCommand` | `db.system=redis`, command + key |
| **Memcached / Memcache** | All `get`/`set`/`delete`/`touch` etc. | `db.system=memcached`, `SET key` |
| **Elasticsearch** | v7, v8, OpenSearch clients | `db.system=elasticsearch` |

## HTTP & RPC

| System | What's traced | Attributes |
|--------|--------------|------------|
| **curl** | `curl_exec`, `curl_multi_*`, `curl_setopt`, header injection | `url.full`, `http.method`, `http.status_code`, `server.address` |
| **HTTP streams** | `fopen`, `file_get_contents` for `http://` URLs | Same HTTP attributes as curl |
| **gRPC** | `Grpc\Call::__construct`, `startBatch` | `db.system=grpc`, method name |
| **SOAP** | `SoapClient::__doRequest` | `db.system=soap`, endpoint + action |

## Messaging

| System | What's traced | Attributes |
|--------|--------------|------------|
| **AMQP / php-amqplib** | `publish`, `consume`, `connect` | `messaging.system=rabbitmq`, exchange/queue, traceparent propagation |
| **rdkafka** | `Producer::produce`, `flush`, `poll`, transactions | `db.system=kafka`, topic |
| **Pheanstalk (Beanstalkd)** | `dispatchCommand` | `db.system=beanstalkd`, command name |

## Frameworks

| Framework | What's traced |
|-----------|--------------|
| **Symfony** | HttpKernel, EventDispatcher, HttpCache, Mailer, Messenger, Console |
| **Laravel** | Kernel, Queue worker, Events, Blade, Eloquent Builder |
| **Shopware 6** | Kernel, DAL (EntityRepository), Cache, Events, Storefront |
| **Doctrine ORM** | `EntityManager::flush` |
| **Twig** | Template render, block render, wrapper |
| **GraphQL** | `executeQuery`, Parser, Executor (webonyx/graphql-php) |

## Built-in PHP functions

Functions that can introduce latency are traced with configurable thresholds —
fast calls below the threshold are dropped so only the slow ones surface.

| Category | Functions | Threshold |
|----------|-----------|-----------|
| **PCRE / Regex** | `preg_match`, `preg_replace`, etc. | 1ms (only slow patterns traced) |
| **Filesystem** | `fopen`, `fread`, `fwrite`, `copy`, `rename`, `mkdir`, `stat`, etc. (30+) | 1ms |
| **Password** | `password_hash` (always), `password_verify` | 1ms |
| **APCu** | `apcu_fetch`, `apcu_store`, `apcu_delete`, etc. | 0.5ms |
| **Session** | `session_start`, `session_write_close` | 1ms |
| **DNS** | `gethostbyname`, `dns_get_record`, `checkdnsrr`, `getmxrr` | Always |
| **Shell** | `exec`, `shell_exec`, `system`, `proc_open`, `popen` | Always |
| **Mail** | `mail`, `mb_send_mail` | Always |
| **Sleep** | `sleep`, `usleep`, `time_nanosleep` | Always |

## Engine-level

| Hook | Purpose | INI gate |
|------|---------|----------|
| **Exception hook** | Catches ALL exceptions at Zend Engine level, marks spans as ERROR | Always on |
| **Compile file** | Profiles `zend_compile_file` time per file | `akari.trace_compile` |
| **GC cycles** | Profiles `gc_collect_cycles` duration | `akari.trace_gc` |
| **Stack trace capture** | Captures full PHP backtrace onto spans on demand | API |

## Span examples

=== "Database"

    ```
    CLIENT PDO::exec
      db.system=mysql
      db.name=mydb
      db.statement=INSERT INTO users VALUES (?, ?)
    ```

=== "HTTP"

    ```
    CLIENT curl_exec
      http.request.method=GET
      url.full=https://api.example.com/users
      server.address=api.example.com
      http.response.status_code=200
    ```

=== "Cache"

    ```
    CLIENT Redis::get
      db.system=redis
      db.statement=GET session:abc123
    ```

=== "Messaging"

    ```
    PRODUCER AMQPExchange::publish
      messaging.system=rabbitmq
      messaging.destination.name=orders
    ```

=== "Exceptions"

    ```
    INTERNAL handleException
      status: ERROR
      events:
        exception.type=RuntimeException
        exception.message=Something went wrong
    ```
