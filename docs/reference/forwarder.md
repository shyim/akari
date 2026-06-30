---
icon: lucide/forward
---

# Forwarder

The forwarder is a small Go service that receives spans and log records from
the extension over UDP (compact msgpack), batches them, and forwards them to
your OTLP collector over HTTP. Spans go to `/v1/traces`; log records go to
`/v1/logs`.

## Running

=== "Docker"

    ```bash
    docker run -d --name akari-forwarder \
      -p 4319:4319/udp \
      -e OTEL_EXPORTER_OTLP_ENDPOINT=http://jaeger:4318 \
      ghcr.io/shyim/akari/akari-forwarder:main
    ```

=== "From source"

    ```bash
    cd forwarder
    go build -o akari-forwarder ./cmd/akari-forwarder
    OTEL_EXPORTER_OTLP_ENDPOINT=http://localhost:4318 ./akari-forwarder
    ```

## Configuration

The forwarder is configured entirely through environment variables.

| Env variable | Default | Description |
|-------------|---------|-------------|
| `OTEL_EXPORTER_OTLP_ENDPOINT` | `http://localhost:4318` | Collector endpoint |
| `OTEL_EXPORTER_OTLP_HEADERS` | *(none)* | Comma-separated `key=value` headers added to every request (e.g. `api-key=secret`); values are percent-decoded |
| `OTEL_FORWARDER_LISTEN` | `127.0.0.1:4319` | UDP listen address |
| `OTEL_FORWARDER_BUFFER_SIZE` | `16384` | Max queued payloads |
| `OTEL_FORWARDER_BATCH_SIZE` | `64` | Payloads per flush |
| `OTEL_FORWARDER_FLUSH_INTERVAL` | `100ms` | Max batching window |

!!! note "Matching the extension"

    `OTEL_FORWARDER_LISTEN` must match the extension's
    [`akari.udp_host` / `akari.udp_port`](../getting-started/configuration.md)
    settings. The defaults line up (`127.0.0.1:4319`) for a forwarder running
    on the same host as PHP.

## Sending to an authenticated backend

For hosted collectors that require an API key, pass it via
`OTEL_EXPORTER_OTLP_HEADERS`:

```bash
OTEL_EXPORTER_OTLP_ENDPOINT=https://api.honeycomb.io \
OTEL_EXPORTER_OTLP_HEADERS=x-honeycomb-team=YOUR_API_KEY \
./akari-forwarder
```
