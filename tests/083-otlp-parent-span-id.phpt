--TEST--
Nested hooked calls have correct parentSpanId linking
--SKIPIF--
<?php
require __DIR__ . '/_skipif.inc';
if (!extension_loaded('pdo_sqlite')) die('skip pdo_sqlite not available');
?>
--INI--
akari.enable=1
akari.service_name=test-svc
--FILE--
<?php
require __DIR__ . '/_doctrine_mock.inc';

// Outer userland span (EntityManager::flush) containing an inner PDO span —
// the canonical APM shape of an ORM write boundary running SQL.
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

$by_name = [];
foreach ($spans as $span) {
    if (strpos($span['name'], 'flush') !== false) {
        $by_name['outer'] = $span;
    }
    if ($span['name'] === 'PDO::query') {
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
