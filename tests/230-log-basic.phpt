--TEST--
Akari\log records a log with severity, body, and context attributes
--SKIPIF--
<?php include __DIR__ . '/_skipif.inc'; ?>
--INI--
akari.enable=1
--FILE--
<?php
Akari\log('warning', 'something happened', ['user_id' => 42, 'feature' => 'checkout']);

$data = json_decode(Akari\getLogsJson(), true);
$records = $data['resourceLogs'][0]['scopeLogs'][0]['logRecords'];

echo 'record count: ' . count($records) . "\n";
$rec = $records[0];
echo 'severityText: ' . $rec['severityText'] . "\n";
echo 'body: ' . $rec['body']['stringValue'] . "\n";

$attrs = array_column($rec['attributes'], 'value', 'key');
echo 'user_id: ' . $attrs['user_id']['stringValue'] . "\n";
echo 'feature: ' . $attrs['feature']['stringValue'] . "\n";

$scope = $data['resourceLogs'][0]['scopeLogs'][0]['scope'];
echo 'scope name: ' . $scope['name'] . "\n";
$res = array_column($data['resourceLogs'][0]['resource']['attributes'], 'value', 'key');
echo 'service.name: ' . $res['service.name']['stringValue'] . "\n";
?>
--EXPECT--
record count: 1
severityText: warning
body: something happened
user_id: 42
feature: checkout
scope name: akari
service.name: php
