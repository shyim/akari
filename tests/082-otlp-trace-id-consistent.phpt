--TEST--
All spans share the same traceId (32 hex chars)
--SKIPIF--
<?php
require __DIR__ . '/_skipif.inc';
?>
--INI--
akari.enable=1
akari.service_name=test-svc
--FILE--
<?php
// Three independent hooked calls produce three sibling spans.
usleep(1);
usleep(1);
usleep(1);

$json = Akari\getSpansJson();
$data = json_decode($json, true);
$spans = $data['resourceSpans'][0]['scopeSpans'][0]['spans'];

$traceIds = [];
foreach ($spans as $span) {
    $traceIds[$span['traceId']] = true;
}

if (count($spans) < 3) {
    echo "FAIL: expected >= 3 spans, got " . count($spans) . "\n";
} elseif (count($traceIds) !== 1) {
    echo "FAIL: expected 1 unique traceId, got " . count($traceIds) . "\n";
} else {
    $traceId = array_key_first($traceIds);
    if (preg_match('/^[0-9a-f]{32}$/', $traceId)) {
        echo "OK: all spans share one traceId (32 hex)\n";
    } else {
        echo "FAIL: traceId is not 32 hex chars: $traceId\n";
    }
}
?>
--EXPECT--
OK: all spans share one traceId (32 hex)
