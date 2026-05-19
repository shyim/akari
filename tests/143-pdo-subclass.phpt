--TEST--
PDO: subclassed PDO still gets instrumented (instanceof matching)
--SKIPIF--
<?php
include __DIR__ . '/_skipif.inc';
if (!extension_loaded('pdo_sqlite')) die('skip pdo_sqlite not available');
?>
--INI--
akari.enable=1
akari.trace_functions=0
--FILE--
<?php
class MyPDO extends PDO {}

$pdo = new MyPDO('sqlite::memory:');
$pdo->query('SELECT 1');

$json = Akari\getSpansJson();
$data = json_decode($json, true);
$spans = $data['resourceSpans'][0]['scopeSpans'][0]['spans'];

// Span name uses the declaring class (PDO), not the subclass (MyPDO)
$found = false;
foreach ($spans as $span) {
    if ($span['name'] === 'PDO::query') {
        $found = true;
        foreach ($span['attributes'] as $attr) {
            if ($attr['key'] === 'db.system') echo "db.system: " . $attr['value']['stringValue'] . "\n";
            if ($attr['key'] === 'db.statement') echo "db.statement: " . $attr['value']['stringValue'] . "\n";
        }
    }
}
echo "found: " . ($found ? 'yes' : 'no') . "\n";
?>
--EXPECT--
db.system: sqlite
db.statement: SELECT 1
found: yes
