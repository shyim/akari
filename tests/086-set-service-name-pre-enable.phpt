--TEST--
setServiceName before Akari\enable() takes effect
--SKIPIF--
<?php include __DIR__ . '/_skipif.inc'; ?>
--INI--
akari.enable=0
akari.trace_functions=1
akari.service_name=ini-default
--FILE--
<?php
// Pre-enable setServiceName should be picked up when Akari\enable() is called
Akari\setServiceName("pre-enable-name");
Akari\enable();

function work() { usleep(1); }
work();

$json = Akari\getSpansJson();
$data = json_decode($json, true);

$svc = "";
$attributes = $data['resourceSpans'][0]['resource']['attributes'] ?? [];
foreach ($attributes as $attr) {
    if ($attr['key'] === 'service.name') {
        $svc = $attr['value']['stringValue'];
        break;
    }
}

if ($svc === 'pre-enable-name') {
    echo "OK: service.name is pre-enable-name\n";
} else {
    echo "FAIL: expected pre-enable-name, got " . var_export($svc, true) . "\n";
}
?>
--EXPECT--
OK: service.name is pre-enable-name
