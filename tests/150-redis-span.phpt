--TEST--
Redis: commands create spans with db.system=redis and key in db.statement
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
$r->set('otel_test_key', 'value');
$r->get('otel_test_key');
$r->del('otel_test_key');
$r->close();

$json = Akari\getSpansJson();
$data = json_decode($json, true);
$spans = $data['resourceSpans'][0]['scopeSpans'][0]['spans'];

$found_set = false;
$found_get = false;
foreach ($spans as $span) {
    if ($span['name'] === 'Redis::set') {
        $found_set = true;
        foreach ($span['attributes'] as $attr) {
            if ($attr['key'] === 'db.system') echo "set db.system: " . $attr['value']['stringValue'] . "\n";
            if ($attr['key'] === 'db.statement') echo "set db.statement: " . $attr['value']['stringValue'] . "\n";
        }
        echo "set kind: " . ($span['kind'] == 3 ? 'CLIENT' : $span['kind']) . "\n";
    }
    if ($span['name'] === 'Redis::get') {
        $found_get = true;
        foreach ($span['attributes'] as $attr) {
            if ($attr['key'] === 'db.statement') echo "get db.statement: " . $attr['value']['stringValue'] . "\n";
        }
    }
}
echo "found set: " . ($found_set ? 'yes' : 'no') . "\n";
echo "found get: " . ($found_get ? 'yes' : 'no') . "\n";
?>
--EXPECT--
set db.system: redis
set db.statement: set otel_test_key
set kind: CLIENT
get db.statement: get otel_test_key
found set: yes
found get: yes
