--TEST--
curl_exec traceparent trace_id matches the current trace
--SKIPIF--
<?php include __DIR__ . '/_curl_skipif.inc'; ?>
--INI--
akari.enable=1
akari.trace_functions=1
--FILE--
<?php
require __DIR__ . '/_curl_server.inc';
$server = akari_curl_test_server_start();

$ch = curl_init($server['base_url'] . "/headers");
curl_setopt($ch, CURLOPT_RETURNTRANSFER, true);
curl_setopt($ch, CURLOPT_TIMEOUT, 10);
$result = curl_exec($ch);
if ($result === false) { echo "FAIL: curl request failed: " . curl_error($ch) . "\n"; exit; }

$data = json_decode($result, true);
$tp = $data["headers"]["Traceparent"] ?? "";

// Extract trace_id from the traceparent header
if (!preg_match('/^00-([0-9a-f]{32})-([0-9a-f]{16})-01$/', $tp, $m)) {
    echo "FAIL: invalid traceparent format\n";
    exit;
}
$tp_trace_id = $m[1];
$tp_span_id = $m[2];

// Get the spans JSON and extract our trace_id
$json = Akari\getSpansJson();
$spans_data = json_decode($json, true);
$spans = $spans_data["resourceSpans"][0]["scopeSpans"][0]["spans"];

// Find the curl_exec span
$curl_span = null;
foreach ($spans as $s) {
    if ($s["name"] === "curl_exec") {
        $curl_span = $s;
        break;
    }
}

if (!$curl_span) {
    echo "FAIL: curl_exec span not found\n";
    exit;
}

// The traceparent trace_id should match the span's trace_id
if ($tp_trace_id === $curl_span["traceId"]) {
    echo "trace_id: matches\n";
} else {
    echo "FAIL: trace_id mismatch: tp=$tp_trace_id span={$curl_span['traceId']}\n";
}

// The traceparent span_id should be the curl_exec span's own span_id
// (so the downstream service creates a child of our curl span)
if ($tp_span_id === $curl_span["spanId"]) {
    echo "span_id: matches curl_exec span\n";
} else {
    echo "FAIL: span_id mismatch: tp=$tp_span_id span={$curl_span['spanId']}\n";
}
?>
--EXPECT--
trace_id: matches
span_id: matches curl_exec span
