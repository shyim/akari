<?php
/**
 * Outgoing HTTP tracing demo with curl.
 * Shows: traceparent injection, url.full, http.request.method, http.response.status_code
 *
 * The traceparent header is automatically injected into outgoing requests,
 * enabling distributed tracing across services.
 */

function fetchUrl(string $url): array {
    $ch = curl_init($url);
    curl_setopt($ch, CURLOPT_RETURNTRANSFER, true);
    curl_setopt($ch, CURLOPT_TIMEOUT, 10);
    curl_setopt($ch, CURLOPT_HTTPHEADER, [
        'Accept: application/json',
        'X-Demo: otel_tracer',
    ]);
    $body = curl_exec($ch);
    $code = curl_getinfo($ch, CURLINFO_HTTP_CODE);
    return ['code' => $code, 'body' => $body];
}

// Make some outgoing HTTP requests
$results = [];
$results[] = fetchUrl('https://httpbin.org/get');
$results[] = fetchUrl('https://httpbin.org/headers');
$results[] = fetchUrl('https://httpbin.org/status/201');

echo "<h2>HTTP Client Tracing Demo</h2>";
foreach ($results as $i => $r) {
    echo "<p>Request " . ($i + 1) . ": HTTP {$r['code']}</p>";
}

// Show the traceparent that was injected
$ch = curl_init('https://httpbin.org/headers');
curl_setopt($ch, CURLOPT_RETURNTRANSFER, true);
curl_setopt($ch, CURLOPT_TIMEOUT, 10);
$headers_response = curl_exec($ch);
$data = json_decode($headers_response, true);
$traceparent = $data['headers']['Traceparent'] ?? 'not found';

echo "<h3>Injected traceparent header</h3>";
echo "<pre>{$traceparent}</pre>";
echo "<p>Format: <code>00-{trace_id}-{span_id}-01</code></p>";
echo "<p><em>Check Jaeger — you'll see CLIENT spans with url.full, http.request.method, and the traceparent links requests to this trace</em></p>";
