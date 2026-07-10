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
| `akari.udp_host` | `127.0.0.1` | Forwarder UDP host |
| `akari.udp_port` | `4319` | Forwarder UDP port |
| `akari.trace_compile` | `0` | Profile file compilation time |
| `akari.trace_gc` | `0` | Profile GC collect cycles time |
| `akari.trace_cli` | `1` | Auto-create a root span for CLI runs, named after the command (e.g. `php console asset:install`, using the script basename). The full command line is recorded as the `process.command_line` attribute. Disable to skip the entry span unless `markAsWebTransaction()` is called |
| `akari.flush_threshold` | `4096` | Completed spans buffered before a mid-request flush |
| `akari.traces_sampler` | *(empty)* | Trace sampler. When unset, falls back to the `OTEL_TRACES_SAMPLER` env var, then to `always_on`. See [Sampling](#sampling) |
| `akari.traces_sampler_arg` | *(empty)* | Ratio for the `traceidratio` samplers (`0.0`–`1.0`). When unset, falls back to `OTEL_TRACES_SAMPLER_ARG`, then to `1.0` |

!!! info "Targeted, APM-style instrumentation"

    Akari only creates spans for the function calls covered by its built-in
    instrumentation hooks (databases, HTTP clients, caches, messaging,
    frameworks) plus the per-request root span. There is no "trace every
    function" or sampling profiler mode — instrumentation is targeted,
    APM-style.

## Recommended configurations

=== "Production (minimum overhead)"

    ```ini
    akari.enable=1
    akari.service_name=app-prod
    akari.min_duration_ms=1      ; drop sub-millisecond spans
    ```

=== "Development (more detail)"

    ```ini
    akari.enable=1
    akari.service_name=app-dev
    akari.trace_compile=1        ; flag slow file compilation
    akari.trace_gc=1             ; flag slow GC cycles
    ```

## Sampling

The sampling decision is made once per request, before any instrumentation
runs — an unsampled request has near-zero overhead (no observer handlers, no
buffering, nothing sent to the forwarder).

`akari.traces_sampler` accepts the standard
[`OTEL_TRACES_SAMPLER`](https://opentelemetry.io/docs/specs/otel/configuration/sdk-environment-variables/#general-sdk-configuration)
values:

| Sampler | Decision |
|---------|----------|
| `always_on` *(default)* | Trace every request |
| `always_off` | Trace nothing |
| `traceidratio` | Trace a deterministic fraction of requests, derived from the trace id (`akari.traces_sampler_arg` = ratio, e.g. `0.1`) |
| `parentbased_always_on` | Follow the incoming `traceparent` sampled flag; trace when there is no parent |
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

!!! note "Differences from the OTel SDK"

    - The default is `always_on` (trace everything), matching Akari's
      behavior before samplers existed — not the SDK default
      `parentbased_always_on`.
    - Unsampled requests do not forward the trace context downstream: no
      hooks run, so no `traceparent` header (with the sampled flag cleared)
      is injected into outbound calls.

## Tuning notes

- **`min_duration_ms`** is the most effective overhead lever in production:
  raising it past `1` discards the long tail of fast spans before they are
  ever serialized or sent.
- **`max_depth`** bounds how deep the call stack is captured. Lower it if you
  have deeply recursive code paths and only care about top-level spans.
- **`udp_host` / `udp_port`** must match the forwarder's
  [`OTEL_FORWARDER_LISTEN`](../reference/forwarder.md) address. The defaults
  (`127.0.0.1:4319`) assume the forwarder runs on the same host.
