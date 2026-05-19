--TEST--
OTLP JSON export includes db and code attributes for different span types
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
        $pdo->query('SELECT 42');
        usleep(100);
    }
}

(new EM())->flush();

$json = Akari\getSpansJson();
$data = json_decode($json, true);

// Validate JSON structure
echo "has resourceSpans: " . (isset($data['resourceSpans']) ? 'yes' : 'no') . "\n";
$spans = $data['resourceSpans'][0]['scopeSpans'][0]['spans'];

foreach ($spans as $span) {
    // Every span must have required fields
    $ok = isset($span['traceId']) && isset($span['spanId']) && isset($span['name'])
        && isset($span['kind']) && isset($span['startTimeUnixNano'])
        && isset($span['endTimeUnixNano']) && isset($span['attributes'])
        && isset($span['status']);
    if (!$ok) {
        echo "FAIL: span '{$span['name']}' missing required fields\n";
    }
}

// Check PDO span has db + code attributes
foreach ($spans as $span) {
    if ($span['name'] === 'PDO::query') {
        $attr_keys = array_column($span['attributes'], 'key');
        echo "PDO has db.system: " . (in_array('db.system', $attr_keys) ? 'yes' : 'no') . "\n";
        echo "PDO has db.statement: " . (in_array('db.statement', $attr_keys) ? 'yes' : 'no') . "\n";
        echo "PDO has code.function: " . (in_array('code.function', $attr_keys) ? 'yes' : 'no') . "\n";
    }
}

// Check the hooked userland span has code attributes
foreach ($spans as $span) {
    if ($span['name'] === 'EM::flush') {
        $attr_keys = array_column($span['attributes'], 'key');
        echo "flush has code.function: " . (in_array('code.function', $attr_keys) ? 'yes' : 'no') . "\n";
        echo "flush has code.filepath: " . (in_array('code.filepath', $attr_keys) ? 'yes' : 'no') . "\n";
        echo "flush has code.lineno: " . (in_array('code.lineno', $attr_keys) ? 'yes' : 'no') . "\n";
    }
}

echo "valid JSON: yes\n";
?>
--EXPECT--
has resourceSpans: yes
PDO has db.system: yes
PDO has db.statement: yes
PDO has code.function: yes
flush has code.function: yes
flush has code.filepath: yes
flush has code.lineno: yes
valid JSON: yes
