--TEST--
Registry: side-effect hooks run without creating spans (curl_setopt)
--SKIPIF--
<?php
include __DIR__ . '/_skipif.inc';
if (!function_exists('curl_init')) die('skip curl not available');
?>
--INI--
akari.enable=1
akari.trace_functions=0
--FILE--
<?php
$ch = curl_init("http://localhost");
curl_setopt($ch, CURLOPT_HTTPHEADER, ["X-Test: hello"]);

$count = Akari\getSpanCount();
echo "spans: " . $count . "\n";
echo "OK: curl_setopt did not create a span\n";
?>
--EXPECT--
spans: 0
OK: curl_setopt did not create a span
