--TEST--
I/O spans inside user functions have correct parentSpanId linking
--SKIPIF--
<?php
include __DIR__ . '/_skipif.inc';
if (!extension_loaded('pdo_sqlite')) die('skip pdo_sqlite not available');
?>
--INI--
akari.enable=1
akari.trace_functions=1
--FILE--
<?php
function doQuery() {
    $pdo = new PDO('sqlite::memory:');
    $pdo->query('SELECT 1');
}
doQuery();

$json = Akari\getSpansJson();
$data = json_decode($json, true);
$spans = $data['resourceSpans'][0]['scopeSpans'][0]['spans'];

$span_map = [];
foreach ($spans as $span) {
    $span_map[$span['name']] = $span;
}

// PDO::query's parent should be doQuery
if (isset($span_map['doQuery']) && isset($span_map['PDO::query'])) {
    $parent_id = $span_map['PDO::query']['parentSpanId'] ?? '';
    $doquery_id = $span_map['doQuery']['spanId'];
    echo "PDO::query parent is doQuery: " . ($parent_id === $doquery_id ? 'yes' : 'no') . "\n";
} else {
    echo "FAIL: missing expected spans\n";
}

// All spans share same traceId
$trace_ids = array_unique(array_column($spans, 'traceId'));
echo "all same traceId: " . (count($trace_ids) === 1 ? 'yes' : 'no') . "\n";
?>
--EXPECT--
PDO::query parent is doQuery: yes
all same traceId: yes
