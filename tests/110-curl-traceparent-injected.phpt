--TEST--
curl_exec injects traceparent header automatically
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
$result = curl_exec($ch);
if (!$result) { echo "SKIP: curl request failed\n"; exit; }

$data = json_decode($result, true);
$tp = $data["headers"]["Traceparent"] ?? "";

// traceparent format: 00-{32hex}-{16hex}-01
if (preg_match('/^00-[0-9a-f]{32}-[0-9a-f]{16}-01$/', $tp)) {
    echo "OK: valid traceparent header injected\n";
} else {
    echo "FAIL: traceparent=$tp\n";
}
?>
--EXPECT--
OK: valid traceparent header injected
