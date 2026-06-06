# akari demo

See traces and logs from PHP applications in Grafana in 30 seconds.

## Quick start

```bash
docker compose up --build
```

Then open:
- **Simple demo app**: http://localhost:8083
- **Symfony Demo**: http://localhost:8082
- **Grafana UI**: http://localhost:3000
- **Tempo API**: http://localhost:3200

## Services

| Service | Port | Description |
|---------|------|-------------|
| Simple PHP demo | [localhost:8083](http://localhost:8083) | Basic pages showing PDO, curl, closures, and `Akari\log()` |
| Symfony Demo | [localhost:8082](http://localhost:8082) | Full Symfony app with route detection |
| Grafana (LGTM) | [localhost:3000](http://localhost:3000) | Traces (Tempo) + logs (Loki) viewer |
| Tempo API | [localhost:3200](http://localhost:3200) | Direct Tempo HTTP API for trace search and fetches |

The forwarder sends spans to the collector's `/v1/traces` and log records to
`/v1/logs`; the bundled Grafana LGTM stack stores traces in Tempo and logs in
Loki.

## Viewing traces

1. Browse the demo apps to generate traffic
2. Open [Grafana](http://localhost:3000) → **Explore**
3. Select the **Tempo** datasource → **Search**, filter by service `demo-php-app` or `symfony-demo`
4. Click a trace to see the span waterfall

## Viewing logs

The `/logging.php` page (and `/complex.php`) emit structured log records with
`Akari\log()`. Each record carries the request's `trace_id` and the active
`span_id`, so logs and traces are linked.

1. Open http://localhost:8083/logging.php to generate some logs
2. In [Grafana](http://localhost:3000) → **Explore**, select the **Loki** datasource
3. Query `{service_name="demo-php-app"}`
4. Expand a log line — `trace_id` / `span_id` are attached; click the trace id to jump to the trace in Tempo

### What you'll see in Symfony Demo traces

- **Root span**: `GET app_blog_index` (route name detected from Symfony)
- **PDO spans**: SQL queries with `db.statement`
- **Framework spans**: `HttpKernel::handle`, controller methods, Twig rendering

## Architecture

```
Browser → PHP/Apache → UDP/msgpack → Go forwarder ──OTLP/HTTP──> Grafana LGTM
                                          ├── /v1/traces → Tempo
                                          └── /v1/logs   → Loki
```
