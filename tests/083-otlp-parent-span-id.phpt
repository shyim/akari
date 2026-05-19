--TEST--
Nested calls have correct parentSpanId linking
--SKIPIF--
<?php
require __DIR__ . '/_skipif.inc';
?>
--INI--
akari.enable=1
akari.trace_functions=1
akari.service_name=test-svc
--FILE--
<?php
function inner() { return 1; }
function outer() { return inner(); }
outer();

$json = Akari\getSpansJson();
$data = json_decode($json, true);
$spans = $data['resourceSpans'][0]['scopeSpans'][0]['spans'];

$by_name = [];
foreach ($spans as $span) {
    if (strpos($span['name'], 'outer') !== false) {
        $by_name['outer'] = $span;
    }
    if (strpos($span['name'], 'inner') !== false) {
        $by_name['inner'] = $span;
    }
}

if (!isset($by_name['outer']) || !isset($by_name['inner'])) {
    echo "FAIL: could not find outer and inner spans\n";
} elseif (($by_name['inner']['parentSpanId'] ?? '') === $by_name['outer']['spanId']) {
    echo "OK: inner's parentSpanId matches outer's spanId\n";
} else {
    echo "FAIL: inner parentSpanId=" . ($by_name['inner']['parentSpanId'] ?? '(none)')
       . " outer spanId=" . $by_name['outer']['spanId'] . "\n";
}
?>
--EXPECT--
OK: inner's parentSpanId matches outer's spanId
