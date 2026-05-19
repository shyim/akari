--TEST--
PDO: query() creates span with db.system and db.statement attributes
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
$pdo = new PDO('sqlite::memory:');
$pdo->exec('CREATE TABLE t (id INTEGER)');
$pdo->query('SELECT * FROM t');

$json = Akari\getSpansJson();
$data = json_decode($json, true);
$spans = $data['resourceSpans'][0]['scopeSpans'][0]['spans'];

foreach ($spans as $span) {
    if ($span['name'] === 'PDO::query') {
        echo "found PDO::query\n";
        echo "kind: " . ($span['kind'] == 3 ? 'CLIENT' : $span['kind']) . "\n";
        foreach ($span['attributes'] as $attr) {
            if ($attr['key'] === 'db.system') {
                echo "db.system: " . $attr['value']['stringValue'] . "\n";
            }
            if ($attr['key'] === 'db.statement') {
                echo "db.statement: " . $attr['value']['stringValue'] . "\n";
            }
        }
    }
}
?>
--EXPECT--
found PDO::query
kind: CLIENT
db.system: sqlite
db.statement: SELECT * FROM t
