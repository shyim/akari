--TEST--
Redis: unknown methods (connect, rawCommand) are not traced
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
$r->close();

$json = Akari\getSpansJson();
if ($json === false) {
    echo "OK: no spans for connect/close\n";
} else {
    $data = json_decode($json, true);
    $spans = $data['resourceSpans'][0]['scopeSpans'][0]['spans'];
    $names = array_column($spans, 'name');
    echo "has connect: " . (in_array('Redis::connect', $names) ? 'yes' : 'no') . "\n";
}
?>
--EXPECT--
OK: no spans for connect/close
