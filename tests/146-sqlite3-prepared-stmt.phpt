--TEST--
SQLite3: prepare() captures SQL and SQLite3Stmt::execute() is a sqlite span
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
$stmt = $db->prepare('SELECT * FROM t WHERE id = ?');
$stmt->bindValue(1, 1, SQLITE3_INTEGER);
$stmt->execute();

$json = Akari\getSpansJson();
$data = json_decode($json, true);
$spans = $data['resourceSpans'][0]['scopeSpans'][0]['spans'];

foreach ($spans as $span) {
    if ($span['name'] === 'SQLite3::prepare') {
        $stmt = null;
        foreach ($span['attributes'] as $attr) {
            if ($attr['key'] === 'db.statement') $stmt = $attr['value']['stringValue'];
        }
        echo "prepare db.statement: $stmt\n";
    }
    if ($span['name'] === 'SQLite3Stmt::execute') {
        $sys = $stmt = null;
        foreach ($span['attributes'] as $attr) {
            if ($attr['key'] === 'db.system') $sys = $attr['value']['stringValue'];
            if ($attr['key'] === 'db.statement') $stmt = $attr['value']['stringValue'];
        }
        echo "execute db.system: $sys\n";
        echo "execute db.statement: $stmt\n";
        echo "execute kind: " . ($span['kind'] == 3 ? 'CLIENT' : $span['kind']) . "\n";
    }
}
?>
--EXPECT--
prepare db.statement: SELECT * FROM t WHERE id = ?
execute db.system: sqlite
execute db.statement: SELECT * FROM t WHERE id = ?
execute kind: CLIENT
