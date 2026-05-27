# Akari — 灯

<p align="center">
  <em>A high-performance PHP APM extension.<br>Automatic distributed tracing — exported as OpenTelemetry.</em>
</p>

> [!WARNING]
> This project is still a work in progress and is not yet ready for production use.
> APIs, configuration, and internals may change at any time.

<p align="center">
  <a href="https://github.com/shyim/akari/actions/workflows/tests.yml"><img src="https://github.com/shyim/akari/actions/workflows/tests.yml/badge.svg" alt="Tests"></a>
  <a href="https://github.com/shyim/akari/actions/workflows/docker.yml"><img src="https://github.com/shyim/akari/actions/workflows/docker.yml/badge.svg" alt="Docker"></a>
  <a href="https://github.com/shyim/akari/pkgs/container/akari%2Fakari-forwarder"><img src="https://img.shields.io/badge/container-ghcr.io-blue" alt="Container"></a>
</p>

---

Akari (灯, "light") is a PHP C extension that illuminates your application's behavior. It hooks into the Zend Engine via the official `zend_observer` API to capture database queries, HTTP requests, cache and messaging calls, framework internals, and exceptions — all exported as standard OpenTelemetry traces to any OTLP-compatible collector (Jaeger, Grafana Tempo, Honeycomb, etc.).

**Key design goals:**
- **Zero-config** for 90% of use cases — drop in, get traces
- **Near-zero overhead** — fire-and-forget UDP, frame deduplication, per-hook thresholds
- **No vendor lock-in** — standard OTLP protocol, use any collector

---

## Architecture

```
PHP-FPM / CLI                        Go Forwarder                    Collector
┌──────────────────┐  UDP/msgpack ┌─────────────────┐  OTLP/HTTP  ┌───────────┐
│                  │──sendto()──> │                 │──POST JSON> │  Jaeger   │
│  akari (Akari)   │  fire&forget │ akari-forwarder │             │  Tempo    │
│                  │  ~1μs        │                 │             │  etc.     │
└──────────────────┘              └─────────────────┘             └───────────┘
```

Spans are serialized as compact msgpack and sent via UDP to a local Go forwarder, which batches and forwards them to your OTLP collector. Log records emitted with `Akari\log()` travel the same UDP path and are forwarded to the collector's `/v1/logs` endpoint (spans go to `/v1/traces`).

---

## Quick Start

### Docker (recommended)

The forwarder is available as a container image:

```bash
docker run -d --name akari-forwarder \
  -p 4319:4319/udp \
  -e OTEL_EXPORTER_OTLP_ENDPOINT=http://jaeger:4318 \
  ghcr.io/shyim/akari/akari-forwarder:main
```

### Build from source

#### 1. Build the extension

```bash
phpize
./configure --enable-akari
make -j$(nproc)
make install
```

#### 2. Build the forwarder

```bash
cd forwarder
go build -o akari-forwarder ./cmd/akari-forwarder
```

#### 3. Configure

```ini
; php.ini
extension=akari.so
akari.enable=1
akari.service_name=my-app
```

#### 4. Start the forwarder

```bash
OTEL_EXPORTER_OTLP_ENDPOINT=http://localhost:4318 ./akari-forwarder
```

Then restart PHP-FPM or run your CLI app. Traces will appear in your collector within seconds.

---

## Instrumentation Coverage

### Database & Cache

| System | What's traced | Attributes |
|--------|--------------|------------|
| **PDO** | `query`, `exec`, `prepare`, `beginTransaction`, `commit`, `rollback` | `db.system`, `db.name`, `db.statement`, `db.user` |
| **MySQLi** | OOP + procedural, `connect`, `prepare`, `execute`, transactions | `db.system=mysql`, SQL statement |
| **OCI8 / Oracle** | `oci_parse`, `oci_execute`, `oci_connect`, `oci_commit`, `oci_rollback` | `db.system=oracle` |
| **Redis / RedisCluster** | 150+ commands with key extraction (phpredis + Relay) | `db.system=redis`, `db.statement=GET key` |
| **Predis** | `Client::executeCommand` | `db.system=redis`, command + key |
| **Memcached / Memcache** | All `get`/`set`/`delete`/`touch` etc. | `db.system=memcached`, `SET key` |
| **Elasticsearch** | v7, v8, OpenSearch clients | `db.system=elasticsearch` |

### HTTP & RPC

| System | What's traced | Attributes |
|--------|--------------|------------|
| **curl** | `curl_exec`, `curl_multi_*`, `curl_setopt`, header injection | `url.full`, `http.method`, `http.status_code`, `server.address` |
| **HTTP streams** | `fopen`, `file_get_contents` for `http://` URLs | Same HTTP attributes as curl |
| **gRPC** | `Grpc\Call::__construct`, `startBatch` | `db.system=grpc`, method name |
| **SOAP** | `SoapClient::__doRequest` | `db.system=soap`, endpoint + action |

### Messaging

| System | What's traced | Attributes |
|--------|--------------|------------|
| **AMQP / php-amqplib** | `publish`, `consume`, `connect` | `messaging.system=rabbitmq`, exchange/queue, traceparent propagation |
| **rdkafka** | `Producer::produce`, `flush`, `poll`, transactions | `db.system=kafka`, topic |
| **Pheanstalk (Beanstalkd)** | `dispatchCommand` | `db.system=beanstalkd`, command name |

### Frameworks

| Framework | What's traced |
|-----------|--------------|
| **Symfony** | HttpKernel, EventDispatcher, HttpCache, Mailer, Messenger, Console |
| **Laravel** | Kernel, Queue worker, Events, Blade, Eloquent Builder |
| **Shopware 6** | Kernel, DAL (EntityRepository), Cache, Events, Storefront |
| **Doctrine ORM** | `EntityManager::flush` |
| **Twig** | Template render, block render, wrapper |
| **GraphQL** | `executeQuery`, Parser, Executor (webonyx/graphql-php) |

### Built-in PHP Functions

Functions that can introduce latency are traced with configurable thresholds:

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

### Engine-Level

| Hook | Purpose | INI Gate |
|------|---------|----------|
| **Exception hook** | Catches ALL exceptions at Zend Engine level, marks spans as ERROR | Always on |
| **Compile file** | Profiles `zend_compile_file` time per file | `akari.trace_compile` |
| **GC cycles** | Profiles `gc_collect_cycles` duration | `akari.trace_gc` |
| **Stack trace capture** | Captures full PHP backtrace onto spans on demand | API |

---

## INI Settings

| Setting | Default | Description |
|---------|---------|-------------|
| `akari.enable` | `0` | Enable tracing |
| `akari.service_name` | `php` | OTel service name |
| `akari.max_depth` | `64` | Max call stack depth (1–256, clamped) |
| `akari.min_duration_ms` | `0` | Drop spans shorter than this (global threshold) |
| `akari.udp_host` | `127.0.0.1` | Forwarder UDP host |
| `akari.udp_port` | `4319` | Forwarder UDP port |
| `akari.trace_compile` | `0` | Profile file compilation time |
| `akari.trace_gc` | `0` | Profile GC collect cycles time |
| `akari.flush_threshold` | `4096` | Completed spans buffered before a mid-request flush |

Akari only creates spans for the function calls covered by its built-in
instrumentation hooks (databases, HTTP clients, caches, messaging, frameworks)
plus the per-request root span. There is no "trace every function" or sampling
profiler mode — instrumentation is targeted, APM-style.

### Recommended Configurations

**Production (minimum overhead):**
```ini
akari.enable=1
akari.service_name=app-prod
akari.min_duration_ms=1      ; drop sub-millisecond spans
```

**Development (more detail):**
```ini
akari.enable=1
akari.service_name=app-dev
akari.trace_compile=1        ; flag slow file compilation
akari.trace_gc=1             ; flag slow GC cycles
```

---

## PHP Userland API

```php
use function Akari\{
    enable, disable, createSpan,
    setTransactionName, getTransactionName, setServiceName,
    addTag, getTags, removeTag, setCustomVariable,
    logException, log, generateDistributedTracingHeaders,
    markAsWebTransaction, markAsCliTransaction,
    isProfiling, getSpanCount, getSpansJson, getLogsJson
};

// Manual control
enable();
createSpan('payment-processing');
setTransactionName('POST /checkout');
addTag('customer_id', '42');
logException($e);

// OTLP logs — PSR-3 style. Each record carries the active trace_id and the
// current (or root) span_id for correlation, and is forwarded to /v1/logs.
log('warning', 'payment retry', ['attempt' => 2, 'gateway' => 'stripe']);

// W3C traceparent header for manual propagation
$headers = generateDistributedTracingHeaders();
// → ['traceparent' => '00-abc123...-def456...-01']

disable();

// Introspection
echo getSpanCount();    // number of spans this request
echo getSpansJson();    // OTLP traces JSON for debugging
echo getLogsJson();     // OTLP logs JSON for debugging
```

---

## Forwarder Configuration

| Env Variable | Default | Description |
|-------------|---------|-------------|
| `OTEL_EXPORTER_OTLP_ENDPOINT` | `http://localhost:4318` | Collector endpoint |
| `OTEL_FORWARDER_LISTEN` | `127.0.0.1:4319` | UDP listen address |
| `OTEL_FORWARDER_BUFFER_SIZE` | `16384` | Max queued payloads |
| `OTEL_FORWARDER_BATCH_SIZE` | `64` | Payloads per flush |
| `OTEL_FORWARDER_FLUSH_INTERVAL` | `100ms` | Max batching window |

---

## Production Readiness

| Area | Status |
|------|--------|
| **Memory safety** | ASAN-verified, no leaks, bounded allocations |
| **Overhead when off** | Zero — `observer_init` returns `{NULL, NULL}` |
| **Exception tracking** | Engine-level hook catches all exceptions, even caught ones |
| **Extensibility conflicts** | Uses official `zend_observer` API — compatible with Xdebug, OPcache |
| **Cross-platform** | macOS (kqueue) + Linux (timer_create / SIGEV_THREAD_ID) |
| **Thread safety (ZTS)** | Full ZTS support via `ZEND_MODULE_GLOBALS` |
| **Span limit** | 256K spans max per request (overflow → warning + drop) |
| **Frame limit** | 64K deduplicated frames max |
| **Stack depth** | Clamped to 256, defaults to 64 |
| **SQL normalization** | Query fingerprinting for grouping in dashboards |

---

## Span Examples

### Database
```
CLIENT PDO::exec
  db.system=mysql
  db.name=mydb
  db.statement=INSERT INTO users VALUES (?, ?)
```

### HTTP
```
CLIENT curl_exec
  http.request.method=GET
  url.full=https://api.example.com/users
  server.address=api.example.com
  http.response.status_code=200
```

### Cache
```
CLIENT Redis::get
  db.system=redis
  db.statement=GET session:abc123
```

### Messaging
```
PRODUCER AMQPExchange::publish
  messaging.system=rabbitmq
  messaging.destination.name=orders
```

### Exceptions
```
INTERNAL handleException
  status: ERROR
  events:
    exception.type=RuntimeException
    exception.message=Something went wrong
```

---

## Project Structure

```
src/
├── profiler.h / profiler.c       # Core state + lifecycle
├── profiler_internal.h           # Shared internal declarations
├── profiler_span.c               # Frame dedup, span management
├── observer.c                    # zend_observer begin/end callbacks
├── hook_registry.c/h             # Modular hook registration system
├── hook_pdo.c                    # PDO instrumentation
├── hook_curl.c                   # curl instrumentation
├── hook_redis.c                  # Redis / RedisCluster / Relay
├── hook_mysqli.c                 # MySQLi (OO + procedural)
├── hook_oci8.c                   # Oracle OCI8
├── hook_memcached.c              # Memcached + Memcache
├── hook_elasticsearch.c          # Elasticsearch v7/v8 + OpenSearch
├── hook_predis.c                 # Predis (pure PHP Redis)
├── hook_grpc.c                   # gRPC
├── hook_rdkafka.c                # rdkafka
├── hook_soap.c                   # SOAP
├── hook_pheanstalk.c             # Pheanstalk / Beanstalkd
├── hook_graphql.c                # webonyx/graphql-php
├── hook_amqp.c                   # AMQP + php-amqplib
├── hook_php_streams.c            # HTTP stream wrappers
├── hook_pcre.c                   # PCRE regex functions
├── hook_io.c                     # Filesystem, password, APCu, DNS, shell, mail, sleep
├── hook_framework.c              # Symfony/Laravel route detection
├── hook_symfony.c                # Symfony components
├── hook_laravel.c                # Laravel components
├── hook_shopware.c               # Shopware 6 DAL + kernel
├── hook_doctrine.c               # Doctrine ORM
├── hook_twig.c                   # Twig templates
├── hook_error.c                  # Framework error handlers
├── hook_engine.c                 # Engine-level hooks (compile, GC, exceptions)
├── hook_root_span.c              # HTTP root span + W3C traceparent
├── php_akari.c             # Module entry, INI, userland API
├── otlp_export.c                 # OTLP JSON serialization
├── udp_export.c                  # UDP/msgpack export
├── sql_normalize.c               # SQL normalization + truncation
└── msgpack_write.h               # Header-only msgpack encoder
forwarder/
├── cmd/akari-forwarder/           # Go entry point
└── internal/                     # Receiver, buffer, transform, forwarder
tests/
└── *.phpt                        # 78 PHPT integration tests
```

---

## Testing

```bash
make test                          # Run all 78 PHPT tests
make test TESTS=tests/010*         # Run specific tests
cd forwarder && go test ./...      # Run Go forwarder tests
```

CI runs automatically on every push and PR via GitHub Actions:
- **[Tests](https://github.com/shyim/akari/actions/workflows/tests.yml)** — PHP 8.2/8.3/8.4/8.5 matrix
- **[Docker](https://github.com/shyim/akari/actions/workflows/docker.yml)** — builds + pushes forwarder image

---

## Requirements

- PHP 8.2+
- macOS or Linux
- Go 1.26+ (for the forwarder)

---

## License

MIT
