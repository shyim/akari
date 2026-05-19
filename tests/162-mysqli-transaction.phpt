--TEST--
MySQLi: begin_transaction/commit create spans
--SKIPIF--
<?php
include __DIR__ . '/_skipif.inc';
if (!extension_loaded('mysqli')) die('skip mysqli not available');
try { $m = new mysqli('127.0.0.1', 'root', '', '', 3306); } catch (Exception $e) { die('skip MySQL server not available: ' . $e->getMessage()); }
if ($m->connect_error) die('skip MySQL server not available');
$m->close();
?>
--INI--
akari.enable=1
akari.trace_functions=0
--FILE--
<?php
$m = new mysqli('127.0.0.1', 'root', '', '', 3306);
$m->begin_transaction();
$m->query('SELECT 1');
$m->commit();
$m->close();

$json = Akari\getSpansJson();
$data = json_decode($json, true);
$spans = $data['resourceSpans'][0]['scopeSpans'][0]['spans'];
$names = array_column($spans, 'name');

echo "has begin_transaction: " . (in_array('mysqli::begin_transaction', $names) ? 'yes' : 'no') . "\n";
echo "has commit: " . (in_array('mysqli::commit', $names) ? 'yes' : 'no') . "\n";
echo "has query: " . (in_array('mysqli::query', $names) ? 'yes' : 'no') . "\n";
?>
--EXPECT--
has begin_transaction: yes
has commit: yes
has query: yes
