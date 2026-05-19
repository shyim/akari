--TEST--
Memcached: set/get create spans with db.system=memcached
--SKIPIF--
<?php
include __DIR__ . '/_skipif.inc';
if (!class_exists('Memcached')) die('skip Memcached extension not available');
$m = new Memcached();
$m->addServer('127.0.0.1', 11211);
if (!$m->set('_otel_test', 1, 1)) die('skip Memcached server not available');
$m->delete('_otel_test');
?>
--INI--
akari.enable=1
akari.trace_functions=0
--FILE--
<?php
$m = new Memcached();
$m->addServer('127.0.0.1', 11211);
$m->set('otel_key', 'value', 10);
$m->get('otel_key');
$m->delete('otel_key');

$json = Akari\getSpansJson();
$data = json_decode($json, true);
$spans = $data['resourceSpans'][0]['scopeSpans'][0]['spans'];

foreach ($spans as $span) {
    if ($span['name'] === 'Memcached::set') {
        echo "found set: yes\n";
        echo "kind: " . ($span['kind'] == 3 ? 'CLIENT' : $span['kind']) . "\n";
        foreach ($span['attributes'] as $attr) {
            if ($attr['key'] === 'db.system') echo "db.system: " . $attr['value']['stringValue'] . "\n";
            if ($attr['key'] === 'db.statement') echo "db.statement: " . $attr['value']['stringValue'] . "\n";
        }
    }
    if ($span['name'] === 'Memcached::get') {
        echo "found get: yes\n";
        foreach ($span['attributes'] as $attr) {
            if ($attr['key'] === 'db.statement') echo "get statement: " . $attr['value']['stringValue'] . "\n";
        }
    }
}
?>
--EXPECT--
found set: yes
kind: CLIENT
db.system: memcached
db.statement: set otel_key
found get: yes
get statement: get otel_key
