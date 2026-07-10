---
icon: lucide/sliders-horizontal
---

# Configuration

Akari is configured entirely through PHP INI settings. The forwarder is
configured separately through environment variables — see
[Forwarder](../reference/forwarder.md).

## INI settings

| Setting | Default | Description |
|---------|---------|-------------|
| `akari.enable` | `0` | Enable tracing |
| `akari.service_name` | *(empty)* | OTel service name. When unset, falls back to the `OTEL_SERVICE_NAME` env var, then to `php` |
| `akari.max_depth` | `64` | Max call stack depth (1–256, clamped) |
| `akari.min_duration_ms` | `0` | Drop spans shorter than this (global threshold) |
| `akari.event_dispatch_min_duration_ms` | `1` | Drop fast Symfony/Shopware event dispatch spans while preserving other instrumentation |
| `akari.sample_rate` | `1.0` | Fraction of requests (0.0–1.0) that collect the full child-span tree. Non-sampled requests still emit the root span and per-layer time summary (see [Head sampling](#head-sampling)). An inbound W3C `traceparent` sampled flag overrides this |
| `akari.disable_at_memory_percentage` | `0` | Safety kill-switch: skip profiling for a request whose peak memory usage already exceeds this percentage of `memory_limit`. `0` disables the check |
| `akari.udp_host` | `127.0.0.1` | Forwarder UDP host |
| `akari.udp_port` | `4319` | Forwarder UDP port |
| `akari.trace_compile` | `0` | Profile file compilation time |
| `akari.trace_gc` | `0` | Profile GC collect cycles time |
| `akari.trace_cli` | `1` | Auto-create a root span for CLI runs, named after the command (e.g. `php console asset:install`, using the script basename). The full command line is recorded as the `process.command_line` attribute. Disable to skip the entry span unless `markAsWebTransaction()` is called |
| `akari.flush_threshold` | `4096` | Completed spans buffered before a mid-request flush |
| `akari.traces_sampler` | *(empty)* | Trace sampler. When unset, falls back to the `OTEL_TRACES_SAMPLER` env var, then to `parentbased_always_on`. See [Trace sampler](#trace-sampler) |
| `akari.traces_sampler_arg` | *(empty)* | Ratio for the `traceidratio` samplers (`0.0`–`1.0`). When unset, falls back to `OTEL_TRACES_SAMPLER_ARG`, then to `1.0` |

!!! info "Targeted, APM-style instrumentation"

    Akari only creates spans for the function calls covered by its built-in
    instrumentation hooks (databases, HTTP clients, caches, messaging,
    frameworks) plus the per-request root span. There is no "trace every
    function" callgraph profiler — instrumentation is targeted, APM-style.

## Head sampling

`akari.sample_rate` controls head sampling: the decision to collect a full
trace is made **once per request, at request start**, before any spans are
built. It is the primary overhead lever for high-throughput production traffic.
It applies to requests the [trace sampler](#trace-sampler) kept — a request the
trace sampler drops is not traced at all, keep-frame or otherwise.

- **Sampled request** (probability `sample_rate`): the full child-span tree is
  collected and exported as before.
- **Non-sampled request** ("keep-frame"): child spans are neither created nor
  kept, so per-request overhead is near zero — but the **root span and the
  per-layer time summary are still exported**. Aggregate metrics (throughput,
  latency, and the app-vs-infrastructure time breakdown) therefore stay
  accurate across *all* traffic, not just the sampled fraction.

Every root span carries a per-layer breakdown as attributes
(`akari.layer.<layer>.duration_ms` and `akari.layer.<layer>.count`) for the
layers `app`, `db`, `cache`, `http`, `template`, `messaging`, `search`,
`compile`, `gc`, and `io`. `app` time is derived as the request's wall-time
minus all instrumented (infrastructure) layer time, and the layer times are
*exclusive* (nested operations are not double-counted). A boolean
`akari.sampled` attribute records whether the request was sampled.

**Distributed tracing:** when a request arrives with a W3C `traceparent`
header, its sampled flag is honored instead of rolling a fresh decision, so a
trace samples consistently end to end.

**Memory safety:** `akari.disable_at_memory_percentage` is an independent
kill-switch. If peak memory usage at request start already exceeds the given
percentage of `memory_limit`, profiling is skipped entirely for that request so
the extension can never push a memory-pressured request into an OOM.

## Recommended configurations

=== "Production (minimum overhead)"

    ```ini
    akari.enable=1
    akari.service_name=app-prod
    akari.sample_rate=0.1                  ; full traces for 10% of requests;
                                           ; layer summary kept for the rest
    akari.min_duration_ms=1                ; drop sub-millisecond spans
    akari.disable_at_memory_percentage=90  ; never trace a near-OOM request
    ```

=== "Development (more detail)"

    ```ini
    akari.enable=1
    akari.service_name=app-dev
    akari.trace_compile=1        ; flag slow file compilation
    akari.trace_gc=1             ; flag slow GC cycles
    ```

## Trace sampler

The trace sampler decides whether a request is traced **at all**, once per
request, before any instrumentation runs — a dropped request has near-zero
overhead (no observer handlers, no buffering, nothing sent to the forwarder).
For requests it keeps, [head sampling](#head-sampling) then chooses between a
full child-span tree and the keep-frame layer summary.

`akari.traces_sampler` accepts the standard
[`OTEL_TRACES_SAMPLER`](https://opentelemetry.io/docs/specs/otel/configuration/sdk-environment-variables/#general-sdk-configuration)
values:

| Sampler | Decision |
|---------|----------|
| `always_on` | Trace every request |
| `always_off` | Trace nothing |
| `traceidratio` | Trace a deterministic fraction of requests, derived from the trace id (`akari.traces_sampler_arg` = ratio, e.g. `0.1`) |
| `parentbased_always_on` *(default)* | Follow the incoming `traceparent` sampled flag; trace when there is no parent |
| `parentbased_always_off` | Follow the incoming `traceparent` sampled flag; drop when there is no parent |
| `parentbased_traceidratio` | Follow the incoming `traceparent` sampled flag; apply the ratio when there is no parent |

The parent context comes from the `traceparent` HTTP header (web requests) or
the `TRACEPARENT` environment variable (CLI runs — the W3C convention for
process propagation). The `traceidratio` decision is computed from the
trace id's random bits, so services sharing a trace reach the same verdict.

!!! example "Trace only what your ingress sampled"

    If your HTTP ingress (load balancer, gateway, mesh) already makes the
    sampling decision, let PHP follow it so you only get complete traces:

    ```ini
    akari.enable=1
    akari.traces_sampler=parentbased_always_off
    ```

    Requests arriving with `traceparent: 00-…-…-01` are traced; requests with
    the sampled flag unset (`…-00`) or without a `traceparent` header are not.

!!! warning "Requests carrying an unsampled traceparent are dropped"

    The default sampler follows the W3C sampled flag: a request arriving with
    `traceparent: 00-…-…-00` is **not** traced. If an upstream proxy sends
    unsampled trace context and you want to trace everything anyway, set
    `akari.traces_sampler=always_on`.

!!! note "Difference from the OTel SDK"

    Unsampled requests do not forward the trace context downstream: no hooks
    run, so no `traceparent` header (with the sampled flag cleared) is
    injected into outbound calls.

## Tuning notes

- **`sample_rate`** is the primary overhead lever for high request volumes.
  Lowering it cuts the cost of the child-span tree on most requests while the
  per-layer summary keeps aggregate dashboards accurate. Start at `0.1` for
  busy services and raise it if you need more per-request detail.
- **`min_duration_ms`** is the next lever: raising it past `1` discards the long
  tail of fast spans before they are ever serialized or sent.
- **`max_depth`** bounds how deep the call stack is captured. Lower it if you
  have deeply recursive code paths and only care about top-level spans.
- **`udp_host` / `udp_port`** must match the forwarder's
  [`OTEL_FORWARDER_LISTEN`](../reference/forwarder.md) address. The defaults
  (`127.0.0.1:4319`) assume the forwarder runs on the same host.
