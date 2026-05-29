--TEST--
SQLite3: exec() and querySingle() are instrumented with db.statement
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
$db->querySingle('SELECT count(*) FROM t');

$json = Akari\getSpansJson();
$data = json_decode($json, true);
$spans = $data['resourceSpans'][0]['scopeSpans'][0]['spans'];

$seen = [];
foreach ($spans as $span) {
    if ($span['name'] === 'SQLite3::exec' || $span['name'] === 'SQLite3::querySingle') {
        $stmt = $sys = null;
        foreach ($span['attributes'] as $attr) {
            if ($attr['key'] === 'db.system') $sys = $attr['value']['stringValue'];
            if ($attr['key'] === 'db.statement') $stmt = $attr['value']['stringValue'];
        }
        $seen[$span['name']] = "$sys|$stmt";
    }
}

ksort($seen);
foreach ($seen as $name => $v) {
    echo "$name: $v\n";
}
?>
--EXPECT--
SQLite3::exec: sqlite|CREATE TABLE t (id INTEGER)
SQLite3::querySingle: sqlite|SELECT count(*) FROM t
