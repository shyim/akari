--TEST--
curl_exec overrides user-set traceparent with the correct one
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
    "traceparent: 00-aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa-bbbbbbbbbbbbbbbb-01",
    "X-Keep-Me: yes",
]);
$result = curl_exec($ch);
if (!$result) { echo "SKIP: curl request failed\n"; exit; }

$data = json_decode($result, true);
$h = $data["headers"];

$tp = $h["Traceparent"] ?? "";
$kept = $h["X-Keep-Me"] ?? "";

// Our traceparent should NOT contain the user's fake aaaa trace_id
if (str_contains($tp, "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa")) {
    echo "FAIL: user traceparent was NOT overridden\n";
} elseif (preg_match('/^00-[0-9a-f]{32}-[0-9a-f]{16}-01$/', $tp)) {
    echo "traceparent: overridden with valid value\n";
} else {
    echo "FAIL: traceparent has invalid format: $tp\n";
}

echo "X-Keep-Me: $kept\n";
?>
--EXPECT--
traceparent: overridden with valid value
X-Keep-Me: yes
