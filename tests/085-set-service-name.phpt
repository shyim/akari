--TEST--
setServiceName override appears in export and doesn't crash
--SKIPIF--
<?php include __DIR__ . '/_skipif.inc'; ?>
--INI--
akari.enable=1
akari.trace_functions=1
akari.service_name=ini-default
--FILE--
<?php
// Verify setServiceName doesn't crash (was Abort trap 6)
Akari\setServiceName("my-override");

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

// Override should take precedence over INI default
if ($svc === 'my-override') {
    echo "OK: service.name is my-override\n";
} else {
    echo "FAIL: expected my-override, got " . var_export($svc, true) . "\n";
}
?>
--EXPECT--
OK: service.name is my-override
