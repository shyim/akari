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
      -p 127.0.0.1:4319:4319/udp \
      -e OTEL_FORWARDER_LISTEN=0.0.0.0:4319 \
      -e AKARI_UDP_KEY \
      -e OTEL_EXPORTER_OTLP_ENDPOINT=http://jaeger:4318 \
      ghcr.io/shyim/akari/akari-forwarder:latest
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
| `OTEL_FORWARDER_LISTEN` | `127.0.0.1:4319` | UDP listen address; non-loopback requires authentication |
| `AKARI_UDP_KEY` | *(none)* | Shared HMAC key, at least 32 bytes; required on network listeners |
| `OTEL_FORWARDER_BUFFER_BYTES` | `16777216` | Maximum queued backing-buffer bytes (up to 256 MiB) |
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

## Transport security

Generate a secret with `openssl rand -hex 32` and configure the same value in
`AKARI_UDP_KEY` for PHP and the forwarder (or use system INI `akari.udp_key` for
PHP). Do not store the secret in a Dockerfile or commit it to the repository.
For PHP-FPM, pass it explicitly to workers if the pool clears its environment.
Docker Compose requires this variable before startup.

When a key is configured, every packet must use the authenticated `AKR1`
envelope: four magic bytes, an unsigned big-endian 64-bit Unix timestamp in
seconds, a 16-byte cryptographic nonce, a 32-byte HMAC-SHA256, then MessagePack.
The MAC covers the first 28 bytes and the complete MessagePack payload. Keys
must contain at least 32 bytes. The receiver rejects timestamps more than 30
seconds from its clock, tampering, replayed packets and unsigned packets. Keep
producer and receiver clocks synchronized. Replay state is bounded and fails
closed if full; it resets when the forwarder restarts.

Unauthenticated operation is restricted to loopback and trusts other processes
on that host. Configure a key on loopback too when those processes are outside
your trust boundary. Authentication does not encrypt UDP: use a private network
or encrypted tunnel between hosts, and HTTPS for remote collectors.

The receiver accepts at most 2,000 packets per second, bounds MessagePack depth
and collection sizes before decoding, and limits queued bytes independently of
packet count. Packets exceeding limits are dropped. A shared key identifies a
trusted producer group; each member can submit telemetry for that group.

Collector redirects are rejected, and collector response reads are capped at
64 KiB. Error bodies are quoted before logging to escape control characters.
