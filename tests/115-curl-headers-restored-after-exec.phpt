--TEST--
User headers are restored after curl_exec (not permanently modified)
--SKIPIF--
<?php include __DIR__ . '/_curl_skipif.inc'; ?>
--INI--
akari.enable=1
--FILE--
<?php
require __DIR__ . '/_curl_server.inc';
$server = akari_curl_test_server_start();

// First request with custom headers
$ch = curl_init($server['base_url'] . "/headers");
curl_setopt($ch, CURLOPT_RETURNTRANSFER, true);
curl_setopt($ch, CURLOPT_TIMEOUT, 10);
curl_setopt($ch, CURLOPT_HTTPHEADER, [
    "X-My-Header: first",
]);
$result1 = curl_exec($ch);
if ($result1 === false) { echo "FAIL: curl request 1 failed: " . curl_error($ch) . "\n"; exit; }

// Reuse same handle for second request — user's X-My-Header should persist
curl_setopt($ch, CURLOPT_URL, $server['base_url'] . "/headers");
$result2 = curl_exec($ch);
if ($result2 === false) { echo "FAIL: curl request 2 failed: " . curl_error($ch) . "\n"; exit; }

$data2 = json_decode($result2, true);
$h = $data2["headers"];

echo "X-My-Header on reuse: " . ($h["X-My-Header"] ?? "MISSING") . "\n";
echo "Traceparent present: " . (!empty($h["Traceparent"]) ? "yes" : "no") . "\n";
?>
--EXPECT--
X-My-Header on reuse: first
Traceparent present: yes
