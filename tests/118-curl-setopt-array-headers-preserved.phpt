--TEST--
curl_exec preserves CURLOPT_HTTPHEADER set via curl_setopt_array (e.g. Symfony HttpClient)
--SKIPIF--
<?php include __DIR__ . '/_curl_skipif.inc'; ?>
--INI--
akari.enable=1
--FILE--
<?php
require __DIR__ . '/_curl_server.inc';
$server = akari_curl_test_server_start();

$ch = curl_init($server['base_url'] . "/headers");
curl_setopt_array($ch, [
    CURLOPT_RETURNTRANSFER => true,
    CURLOPT_TIMEOUT => 10,
    CURLOPT_HTTPHEADER => [
        "Content-Type: application/json",
        "X-Custom-One: value1",
    ],
]);
$result = curl_exec($ch);
if ($result === false) { echo "FAIL: curl request failed: " . curl_error($ch) . "\n"; exit; }

$data = json_decode($result, true);
$h = $data["headers"];

$ok = true;
if (($h["Content-Type"] ?? "") !== "application/json") { echo "FAIL: Content-Type clobbered\n"; $ok = false; }
if (($h["X-Custom-One"] ?? "") !== "value1") { echo "FAIL: X-Custom-One missing\n"; $ok = false; }
if (empty($h["Traceparent"])) { echo "FAIL: Traceparent missing\n"; $ok = false; }

if ($ok) echo "OK: setopt_array headers + traceparent all present\n";
?>
--EXPECT--
OK: setopt_array headers + traceparent all present
