--TEST--
PDO: beginTransaction/commit create spans
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
$pdo->beginTransaction();
$pdo->exec('INSERT INTO t VALUES (1)');
$pdo->commit();

$json = Akari\getSpansJson();
$data = json_decode($json, true);
$spans = $data['resourceSpans'][0]['scopeSpans'][0]['spans'];
$names = array_column($spans, 'name');

echo "has beginTransaction: " . (in_array('PDO::beginTransaction', $names) ? 'yes' : 'no') . "\n";
echo "has commit: " . (in_array('PDO::commit', $names) ? 'yes' : 'no') . "\n";
echo "has exec: " . (in_array('PDO::exec', $names) ? 'yes' : 'no') . "\n";
?>
--EXPECT--
has beginTransaction: yes
has commit: yes
has exec: yes
