--TEST--
explicit akari.service_name takes precedence over OTEL_SERVICE_NAME env
--SKIPIF--
<?php include __DIR__ . '/_skipif.inc'; ?>
--ENV--
OTEL_SERVICE_NAME=from-env-app
--INI--
akari.enable=1
akari.service_name=from-ini-app
--FILE--
<?php
usleep(1);

$json = Akari\getSpansJson();
$data = json_decode($json, true);

$value = null;
$attributes = $data['resourceSpans'][0]['resource']['attributes'] ?? [];
foreach ($attributes as $attr) {
    if ($attr['key'] === 'service.name') {
        $value = $attr['value']['stringValue'];
        break;
    }
}

echo "service.name: $value\n";
?>
--EXPECT--
service.name: from-ini-app
