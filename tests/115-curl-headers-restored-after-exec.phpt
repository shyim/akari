--TEST--
User headers are restored after curl_exec (not permanently modified)
--SKIPIF--
<?php
include __DIR__ . '/_skipif.inc';
if (!function_exists('curl_init')) die('skip curl not available');
?>
--INI--
akari.enable=1
--FILE--
<?php
// First request with custom headers
$ch = curl_init("https://httpbin.org/headers");
curl_setopt($ch, CURLOPT_RETURNTRANSFER, true);
curl_setopt($ch, CURLOPT_TIMEOUT, 10);
curl_setopt($ch, CURLOPT_HTTPHEADER, [
    "X-My-Header: first",
]);
$result1 = curl_exec($ch);
if (!$result1) { echo "SKIP: curl request 1 failed\n"; exit; }

// Reuse same handle for second request — user's X-My-Header should persist
curl_setopt($ch, CURLOPT_URL, "https://httpbin.org/headers");
$result2 = curl_exec($ch);
if (!$result2) { echo "SKIP: curl request 2 failed\n"; exit; }

$data2 = json_decode($result2, true);
$h = $data2["headers"];

echo "X-My-Header on reuse: " . ($h["X-My-Header"] ?? "MISSING") . "\n";
echo "Traceparent present: " . (!empty($h["Traceparent"]) ? "yes" : "no") . "\n";
?>
--EXPECT--
X-My-Header on reuse: first
Traceparent present: yes
