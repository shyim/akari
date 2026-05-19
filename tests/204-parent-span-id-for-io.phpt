--TEST--
Internal hook spans inside a hooked userland span link via parentSpanId
--SKIPIF--
<?php
include __DIR__ . '/_skipif.inc';
if (!extension_loaded('pdo_sqlite')) die('skip pdo_sqlite not available');
?>
--INI--
akari.enable=1
--FILE--
<?php
require __DIR__ . '/_doctrine_mock.inc';

class EM implements \Doctrine\ORM\EntityManagerInterface {
    public function flush() {
        $pdo = new PDO('sqlite::memory:');
        $pdo->query('SELECT 1');
    }
}

(new EM())->flush();

$json = Akari\getSpansJson();
$data = json_decode($json, true);
$spans = $data['resourceSpans'][0]['scopeSpans'][0]['spans'];

$span_map = [];
foreach ($spans as $span) {
    $span_map[$span['name']] = $span;
}

// PDO::query's parent should be EM::flush
if (isset($span_map['EM::flush']) && isset($span_map['PDO::query'])) {
    $parent_id = $span_map['PDO::query']['parentSpanId'] ?? '';
    $flush_id = $span_map['EM::flush']['spanId'];
    echo "PDO::query parent is EM::flush: " . ($parent_id === $flush_id ? 'yes' : 'no') . "\n";
} else {
    echo "FAIL: missing expected spans\n";
}

// All spans share same traceId
$trace_ids = array_unique(array_column($spans, 'traceId'));
echo "all same traceId: " . (count($trace_ids) === 1 ? 'yes' : 'no') . "\n";
?>
--EXPECT--
PDO::query parent is EM::flush: yes
all same traceId: yes
