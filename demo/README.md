# akari demo

See traces from PHP applications in Jaeger in 30 seconds.

## Quick start

```bash
cd demo
docker compose up --build
```

Then open:
- **Simple demo app**: http://localhost:8081
- **Symfony Demo**: http://localhost:8082
- **Jaeger UI**: http://localhost:16686

## Services

| Service | Port | Description |
|---------|------|-------------|
| Simple PHP demo | [localhost:8081](http://localhost:8081) | Basic pages showing PDO, curl, closures |
| Symfony Demo | [localhost:8082](http://localhost:8082) | Full Symfony app with route detection |
| Jaeger UI | [localhost:16686](http://localhost:16686) | Trace viewer |

## Viewing traces

1. Browse the demo apps to generate traffic
2. Open [Jaeger UI](http://localhost:16686)
3. Select service **demo-php-app** or **symfony-demo**
4. Click **Find Traces**
5. Click a trace to see the span waterfall

### What you'll see in Symfony Demo traces

- **Root span**: `GET app_blog_index` (route name detected from Symfony)
- **PDO spans**: SQL queries with `db.statement`
- **Framework spans**: `HttpKernel::handle`, controller methods, Twig rendering

## Architecture

```
Browser → PHP/Apache → UDP/msgpack → Go forwarder → OTLP/HTTP → Jaeger
```
