---
icon: lucide/rocket
---

# Quick start

The fastest way to see Akari in action is to run the pre-built forwarder
container and point it at an OTLP collector you already have.

## 1. Run the forwarder (Docker)

The forwarder is published as a container image on GHCR:

```bash
docker run -d --name akari-forwarder \
  -p 4319:4319/udp \
  -e OTEL_EXPORTER_OTLP_ENDPOINT=http://jaeger:4318 \
  ghcr.io/shyim/akari/akari-forwarder:main
```

This listens for spans on UDP `4319` and forwards them to the collector at
`OTEL_EXPORTER_OTLP_ENDPOINT`. See [Forwarder](../reference/forwarder.md) for
the full list of environment variables.

## 2. Enable the extension

Add Akari to your `php.ini` (see [Installation](installation.md) for how to
build `akari.so`):

```ini
extension=akari.so
akari.enable=1
akari.service_name=my-app
```

## 3. Generate traffic

Restart PHP-FPM or run your CLI app. Traces will appear in your collector
within seconds.

## Try the full demo stack

The repository ships a `demo/` directory with a complete Grafana LGTM
(Tempo + Loki) stack, a simple PHP app, and a Symfony Demo app — wired up end
to end:

```bash
cd demo
docker compose up --build
```

Then open:

| Service | URL | Description |
|---------|-----|-------------|
| Simple PHP demo | <http://localhost:8083> | PDO, curl, closures, and `Akari\log()` |
| Symfony Demo | <http://localhost:8082> | Full Symfony app with route detection |
| Grafana (LGTM) | <http://localhost:3000> | Traces (Tempo) + logs (Loki) viewer |
| Tempo API | <http://localhost:3200> | Direct Tempo HTTP API |

Browse the demo apps to generate traffic, then open **Grafana → Explore**,
select the **Tempo** datasource, and search by service name to see the span
waterfall.

!!! tip "Linking logs to traces"

    The demo's `/logging.php` page emits structured records with
    `Akari\log()`. Each record carries the request's `trace_id` and the active
    `span_id`, so you can jump straight from a Loki log line to the
    corresponding trace in Tempo.
