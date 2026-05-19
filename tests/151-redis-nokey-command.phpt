--TEST--
Redis: no-key commands (ping) create spans without key in statement
--SKIPIF--
<?php
include __DIR__ . '/_skipif.inc';
if (!class_exists('Redis')) die('skip Redis extension not available');
$r = new Redis();
if (!@$r->connect('127.0.0.1', 6379, 0.5)) die('skip Redis server not available');
$r->close();
?>
--INI--
akari.enable=1
akari.trace_functions=0
--FILE--
<?php
$r = new Redis();
$r->connect('127.0.0.1', 6379);
$r->ping();
$r->close();

$json = Akari\getSpansJson();
$data = json_decode($json, true);
$spans = $data['resourceSpans'][0]['scopeSpans'][0]['spans'];

foreach ($spans as $span) {
    if ($span['name'] === 'Redis::ping') {
        echo "found ping: yes\n";
        foreach ($span['attributes'] as $attr) {
            if ($attr['key'] === 'db.statement') echo "db.statement: " . $attr['value']['stringValue'] . "\n";
        }
    }
}
?>
--EXPECT--
found ping: yes
db.statement: ping
