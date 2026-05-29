--TEST--
SQLite3: query() creates CLIENT span with db.system and db.statement
--SKIPIF--
<?php
include __DIR__ . '/_skipif.inc';
if (!extension_loaded('sqlite3')) die('skip sqlite3 not available');
?>
--INI--
akari.enable=1
akari.trace_functions=0
--FILE--
<?php
$db = new SQLite3(':memory:');
$db->exec('CREATE TABLE t (id INTEGER)');
$db->query('SELECT * FROM t');

$json = Akari\getSpansJson();
$data = json_decode($json, true);
$spans = $data['resourceSpans'][0]['scopeSpans'][0]['spans'];

foreach ($spans as $span) {
    if ($span['name'] === 'SQLite3::query') {
        echo "found SQLite3::query\n";
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
found SQLite3::query
kind: CLIENT
db.system: sqlite
db.statement: SELECT * FROM t
