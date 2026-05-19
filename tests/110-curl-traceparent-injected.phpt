--TEST--
curl_exec injects traceparent header automatically
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
$result = curl_exec($ch);
if ($result === false) { echo "FAIL: curl request failed: " . curl_error($ch) . "\n"; exit; }

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
