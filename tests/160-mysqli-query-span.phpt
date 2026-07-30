--TEST--
MySQLi: OO query() creates span with db.system=mysql and SQL in db.statement
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
$m->query('SELECT 1');
$m->close();

$json = Akari\getSpansJson();
$data = json_decode($json, true);
$spans = $data['resourceSpans'][0]['scopeSpans'][0]['spans'];

foreach ($spans as $span) {
    if ($span['name'] === 'mysqli::query') {
        echo "found: yes\n";
        echo "kind: " . ($span['kind'] == 3 ? 'CLIENT' : $span['kind']) . "\n";
        foreach ($span['attributes'] as $attr) {
            if ($attr['key'] === 'db.system') echo "db.system: " . $attr['value']['stringValue'] . "\n";
            if ($attr['key'] === 'db.statement') echo "db.statement: " . $attr['value']['stringValue'] . "\n";
        }
    }
}
?>
--EXPECT--
found: yes
kind: CLIENT
db.system: mysql
db.statement: SELECT ?
