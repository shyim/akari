--TEST--
PDO: prepared statement execute() captures query string
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
$stmt = $pdo->prepare('SELECT * FROM t WHERE id = ?');
$stmt->execute([1]);

$json = Akari\getSpansJson();
$data = json_decode($json, true);
$spans = $data['resourceSpans'][0]['scopeSpans'][0]['spans'];

foreach ($spans as $span) {
    if ($span['name'] === 'PDOStatement::execute') {
        echo "found PDOStatement::execute\n";
        foreach ($span['attributes'] as $attr) {
            if ($attr['key'] === 'db.statement') {
                echo "db.statement: " . $attr['value']['stringValue'] . "\n";
            }
        }
    }
}
?>
--EXPECT--
found PDOStatement::execute
db.statement: SELECT * FROM t WHERE id = ?
