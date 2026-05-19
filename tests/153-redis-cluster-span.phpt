--TEST--
RedisCluster: commands create spans with db.system=redis
--SKIPIF--
<?php
include __DIR__ . '/_skipif.inc';
if (!class_exists('RedisCluster')) die('skip RedisCluster not available');
try { $r = new RedisCluster(null, ['127.0.0.1:7000'], 0.5); } catch (Exception $e) { die('skip RedisCluster not available: ' . $e->getMessage()); }
?>
--INI--
akari.enable=1
akari.trace_functions=0
--FILE--
<?php
$r = new RedisCluster(null, ['127.0.0.1:7000']);
$r->set('otel_cluster_test', 'value');
$r->get('otel_cluster_test');
$r->del('otel_cluster_test');

$json = Akari\getSpansJson();
$data = json_decode($json, true);
$spans = $data['resourceSpans'][0]['scopeSpans'][0]['spans'];

$found_set = false;
foreach ($spans as $span) {
    if ($span['name'] === 'RedisCluster::set') {
        $found_set = true;
        foreach ($span['attributes'] as $attr) {
            if ($attr['key'] === 'db.system') echo "db.system: " . $attr['value']['stringValue'] . "\n";
            if ($attr['key'] === 'db.statement') echo "db.statement: " . $attr['value']['stringValue'] . "\n";
        }
    }
}
echo "found set: " . ($found_set ? 'yes' : 'no') . "\n";
?>
--EXPECT--
db.system: redis
db.statement: set otel_cluster_test
found set: yes
