---
icon: lucide/code
---

# PHP userland API

While Akari is zero-config for most use cases, it exposes a userland API under
the `Akari\` namespace for manual control, custom spans, tagging, structured
logging, and distributed trace propagation.

```php
use function Akari\{
    enable, disable, createSpan,
    setTransactionName, getTransactionName, setServiceName,
    addTag, removeTag, setCustomVariable,
    logException, log, generateDistributedTracingHeaders,
    markAsWebTransaction, markAsCliTransaction
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
```

## Function reference

| Function | Purpose |
|----------|---------|
| `enable()` / `disable()` | Turn tracing on or off at runtime |
| `createSpan($name)` | Open a custom span |
| `setTransactionName($name)` / `getTransactionName()` | Override / read the root transaction name |
| `setServiceName($name)` | Override the OTel service name for the current request |
| `addTag($key, $value)` / `removeTag($key)` | Attach or remove an attribute on the active span |
| `setCustomVariable($key, $value)` | Attach a custom variable to the trace |
| `logException($e)` | Record an exception as a span event with `status: ERROR` |
| `log($level, $message, $context)` | Emit a structured OTLP log record (PSR-3 style) correlated with the active trace |
| `generateDistributedTracingHeaders()` | Return a W3C `traceparent` header for manual propagation across services |
| `markAsWebTransaction()` / `markAsCliTransaction()` | Force the request to be treated as a web or CLI transaction |

## Structured logging

`Akari\log()` emits OTLP log records that travel the same UDP path as spans and
are forwarded to the collector's `/v1/logs` endpoint. Each record automatically
carries the active `trace_id` and the current (or root) `span_id`, so logs and
traces stay correlated in your backend:

```php
Akari\log('error', 'checkout failed', [
    'order_id' => $order->id,
    'gateway'  => 'stripe',
]);
```

## Distributed tracing

To propagate a trace across service boundaries, attach the generated
`traceparent` header to outbound requests:

```php
$headers = Akari\generateDistributedTracingHeaders();
// → ['traceparent' => '00-<trace-id>-<span-id>-01']

// e.g. forward it on an outgoing HTTP call
$client->request('GET', $url, ['headers' => $headers]);
```

Inbound `traceparent` headers on HTTP requests are picked up automatically by
the root-span hook, so manual propagation is only needed for protocols Akari
does not already instrument.

## Debug introspection (debug builds only)

These functions are compiled in **only** when the extension is built with
`--enable-akari-debug`. They are used by the test suite and for local
debugging, and are absent from production builds:

```php
use function Akari\{
    isProfiling, getSpanCount, getFrameCount, getTags,
    getSpansJson, getLogsJson
};

echo getSpanCount();    // number of spans this request
echo getSpansJson();    // OTLP traces JSON for debugging
echo getLogsJson();     // OTLP logs JSON for debugging
```

See [Installation](../getting-started/installation.md#debug-builds)
for how to enable debug builds.
