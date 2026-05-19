--TEST--
curl_exec preserves user-set CURLOPT_HTTPHEADER alongside traceparent
--SKIPIF--
<?php
include __DIR__ . '/_skipif.inc';
if (!function_exists('curl_init')) die('skip curl not available');
?>
--INI--
akari.enable=1
--FILE--
<?php
$ch = curl_init("https://httpbin.org/headers");
curl_setopt($ch, CURLOPT_RETURNTRANSFER, true);
curl_setopt($ch, CURLOPT_TIMEOUT, 10);
curl_setopt($ch, CURLOPT_HTTPHEADER, [
    "X-Custom-One: value1",
    "X-Custom-Two: value2",
]);
$result = curl_exec($ch);
if (!$result) { echo "SKIP: curl request failed\n"; exit; }

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
