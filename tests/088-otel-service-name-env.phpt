--TEST--
service.name falls back to OTEL_SERVICE_NAME env when akari.service_name is unset
--SKIPIF--
<?php include __DIR__ . '/_skipif.inc'; ?>
--ENV--
OTEL_SERVICE_NAME=from-env-app
--INI--
akari.enable=1
--FILE--
<?php
usleep(1);

$json = Akari\getSpansJson();
$data = json_decode($json, true);

$found = false;
$attributes = $data['resourceSpans'][0]['resource']['attributes'] ?? [];
foreach ($attributes as $attr) {
    if ($attr['key'] === 'service.name' &&
        $attr['value']['stringValue'] === 'from-env-app') {
        $found = true;
        break;
    }
}

echo $found ? "OK: service.name is from-env-app\n" : "FAIL: service.name not found or wrong\n";
?>
--EXPECT--
OK: service.name is from-env-app
