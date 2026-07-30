--TEST--
MySQLi: procedural mysqli_query() creates span with SQL
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
$link = mysqli_connect('127.0.0.1', 'root', '', '', 3306);
mysqli_query($link, 'SELECT 42');
mysqli_close($link);

$json = Akari\getSpansJson();
$data = json_decode($json, true);
$spans = $data['resourceSpans'][0]['scopeSpans'][0]['spans'];

foreach ($spans as $span) {
    if ($span['name'] === 'mysqli_query') {
        echo "found: yes\n";
        foreach ($span['attributes'] as $attr) {
            if ($attr['key'] === 'db.system') echo "db.system: " . $attr['value']['stringValue'] . "\n";
            if ($attr['key'] === 'db.statement') echo "db.statement: " . $attr['value']['stringValue'] . "\n";
        }
    }
}
?>
--EXPECT--
found: yes
db.system: mysql
db.statement: SELECT ?
