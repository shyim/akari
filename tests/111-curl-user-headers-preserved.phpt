--TEST--
curl_exec preserves user-set CURLOPT_HTTPHEADER alongside traceparent
--SKIPIF--
<?php include __DIR__ . '/_curl_skipif.inc'; ?>
--INI--
akari.enable=1
--FILE--
<?php
require __DIR__ . '/_curl_server.inc';
$server = akari_curl_test_server_start();

$ch = curl_init($server['base_url'] . "/headers");
curl_setopt($ch, CURLOPT_RETURNTRANSFER, true);
curl_setopt($ch, CURLOPT_TIMEOUT, 10);
curl_setopt($ch, CURLOPT_HTTPHEADER, [
    "X-Custom-One: value1",
    "X-Custom-Two: value2",
]);
$result = curl_exec($ch);
if ($result === false) { echo "FAIL: curl request failed: " . curl_error($ch) . "\n"; exit; }

$data = json_decode($result, true);
$h = $data["headers"];

$ok = true;
if (($h["X-Custom-One"] ?? "") !== "value1") { echo "FAIL: X-Custom-One missing\n"; $ok = false; }
if (($h["X-Custom-Two"] ?? "") !== "value2") { echo "FAIL: X-Custom-Two missing\n"; $ok = false; }
if (empty($h["Traceparent"])) { echo "FAIL: Traceparent missing\n"; $ok = false; }

if ($ok) echo "OK: user headers + traceparent all present\n";
?>
--EXPECT--
OK: user headers + traceparent all present
