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

## Tuning notes

- **`min_duration_ms`** is the most effective overhead lever in production:
  raising it past `1` discards the long tail of fast spans before they are
  ever serialized or sent.
- **`max_depth`** bounds how deep the call stack is captured. Lower it if you
  have deeply recursive code paths and only care about top-level spans.
- **`udp_host` / `udp_port`** must match the forwarder's
  [`OTEL_FORWARDER_LISTEN`](../reference/forwarder.md) address. The defaults
  (`127.0.0.1:4319`) assume the forwarder runs on the same host.
