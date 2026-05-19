--TEST--
OTLP JSON export includes db and code attributes for different span types
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
function queryDb() {
    $pdo = new PDO('sqlite::memory:');
    $pdo->query('SELECT 42');
    usleep(100);
}
queryDb();

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

// Check PDO span has db attributes
foreach ($spans as $span) {
    if ($span['name'] === 'PDO::query') {
        $attr_keys = array_column($span['attributes'], 'key');
        echo "PDO has db.system: " . (in_array('db.system', $attr_keys) ? 'yes' : 'no') . "\n";
        echo "PDO has db.statement: " . (in_array('db.statement', $attr_keys) ? 'yes' : 'no') . "\n";
        echo "PDO has code.function: " . (in_array('code.function', $attr_keys) ? 'yes' : 'no') . "\n";
    }
}

// Check user function has code attributes
foreach ($spans as $span) {
    if ($span['name'] === 'queryDb') {
        $attr_keys = array_column($span['attributes'], 'key');
        echo "queryDb has code.function: " . (in_array('code.function', $attr_keys) ? 'yes' : 'no') . "\n";
        echo "queryDb has code.filepath: " . (in_array('code.filepath', $attr_keys) ? 'yes' : 'no') . "\n";
        echo "queryDb has code.lineno: " . (in_array('code.lineno', $attr_keys) ? 'yes' : 'no') . "\n";
    }
}

echo "valid JSON: yes\n";
?>
--EXPECT--
has resourceSpans: yes
PDO has db.system: yes
PDO has db.statement: yes
PDO has code.function: yes
queryDb has code.function: yes
queryDb has code.filepath: yes
queryDb has code.lineno: yes
valid JSON: yes
