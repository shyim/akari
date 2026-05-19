--TEST--
service.name attribute matches akari.service_name
--SKIPIF--
<?php
require __DIR__ . '/_skipif.inc';
?>
--INI--
akari.enable=1
akari.service_name=my-test-app
--FILE--
<?php
usleep(1);

$json = Akari\getSpansJson();
$data = json_decode($json, true);

$found = false;
$attributes = $data['resourceSpans'][0]['resource']['attributes'] ?? [];
foreach ($attributes as $attr) {
    if ($attr['key'] === 'service.name' &&
        $attr['value']['stringValue'] === 'my-test-app') {
        $found = true;
        break;
    }
}

echo $found ? "OK: service.name is my-test-app\n" : "FAIL: service.name not found or wrong\n";
?>
--EXPECT--
OK: service.name is my-test-app
