--TEST--
Multiple curl_exec calls get unique span_ids in traceparent
--SKIPIF--
<?php
include __DIR__ . '/_skipif.inc';
if (!function_exists('curl_init')) die('skip curl not available');
?>
--INI--
akari.enable=1
--FILE--
<?php
$traceparents = [];

for ($i = 0; $i < 3; $i++) {
    $ch = curl_init("https://httpbin.org/headers");
    curl_setopt($ch, CURLOPT_RETURNTRANSFER, true);
    curl_setopt($ch, CURLOPT_TIMEOUT, 10);
    $result = curl_exec($ch);
    if (!$result) { echo "SKIP: curl request $i failed\n"; exit; }
    $data = json_decode($result, true);
    $traceparents[] = $data["headers"]["Traceparent"] ?? "";
}

// All should have the same trace_id but different span_ids
$trace_ids = [];
$span_ids = [];
foreach ($traceparents as $tp) {
    if (preg_match('/^00-([0-9a-f]{32})-([0-9a-f]{16})-01$/', $tp, $m)) {
        $trace_ids[] = $m[1];
        $span_ids[] = $m[2];
    }
}

// Same trace_id
$unique_traces = array_unique($trace_ids);
echo "same trace_id: " . (count($unique_traces) === 1 ? "yes" : "no") . "\n";

// Unique span_ids
$unique_spans = array_unique($span_ids);
echo "unique span_ids: " . (count($unique_spans) === 3 ? "yes" : "no") . "\n";
?>
--EXPECT--
same trace_id: yes
unique span_ids: yes
