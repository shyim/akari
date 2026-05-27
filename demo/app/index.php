<?php
/**
 * akari demo application
 *
 * Hit these endpoints to generate traces (and logs) visible in Grafana:
 *   http://localhost:8083/                 — this page (links)
 *   http://localhost:8083/hello.php        — simple function tracing
 *   http://localhost:8083/database.php     — PDO/SQLite queries
 *   http://localhost:8083/http-client.php  — outgoing curl with traceparent
 *   http://localhost:8083/logging.php      — OTLP logs via Akari\log()
 *   http://localhost:8083/complex.php      — all features combined
 *
 * Grafana UI: http://localhost:3000 (Explore → Tempo for traces, Loki for logs)
 */
?>
<!DOCTYPE html>
<html>
<head><title>akari demo</title></head>
<body>
<h1>akari demo</h1>
<p>Extension loaded: <strong><?= extension_loaded('akari') ? 'yes' : 'no' ?></strong></p>
<ul>
    <li><a href="/hello.php">Simple function tracing</a></li>
    <li><a href="/database.php">Database queries (PDO/SQLite)</a></li>
    <li><a href="/http-client.php">Outgoing HTTP (curl + traceparent)</a></li>
    <li><a href="/logging.php">OTLP logs (Akari\log)</a></li>
    <li><a href="/complex.php">All features combined</a></li>
</ul>
<p>Open <a href="http://localhost:3000" target="_blank">Grafana</a> to see traces (Tempo) and logs (Loki).</p>
</body>
</html>
