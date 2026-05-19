<?php
/**
 * otel_tracer demo application
 *
 * Hit these endpoints to generate traces visible in Jaeger UI:
 *   http://localhost:8081/                 — this page (links)
 *   http://localhost:8081/hello.php        — simple function tracing
 *   http://localhost:8081/database.php     — PDO/SQLite queries
 *   http://localhost:8081/http-client.php  — outgoing curl with traceparent
 *   http://localhost:8081/complex.php      — all features combined
 *
 * Jaeger UI: http://localhost:16686
 */
?>
<!DOCTYPE html>
<html>
<head><title>otel_tracer demo</title></head>
<body>
<h1>otel_tracer demo</h1>
<p>Extension loaded: <strong><?= extension_loaded('otel_tracer') ? 'yes' : 'no' ?></strong></p>
<ul>
    <li><a href="/hello.php">Simple function tracing</a></li>
    <li><a href="/database.php">Database queries (PDO/SQLite)</a></li>
    <li><a href="/http-client.php">Outgoing HTTP (curl + traceparent)</a></li>
    <li><a href="/complex.php">All features combined</a></li>
</ul>
<p>Open <a href="http://localhost:16686" target="_blank">Jaeger UI</a> to see traces.</p>
</body>
</html>
