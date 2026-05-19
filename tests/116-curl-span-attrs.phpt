--TEST--
curl_exec span has url.full, http.request.method, http.response.status_code attributes
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
$ch = curl_init("https://httpbin.org/status/200");
curl_setopt($ch, CURLOPT_RETURNTRANSFER, true);
curl_setopt($ch, CURLOPT_TIMEOUT, 10);
$result = curl_exec($ch);
if (curl_errno($ch)) { echo "SKIP: curl failed\n"; exit; }

$json = Akari\getSpansJson();
$data = json_decode($json, true);
$spans = $data['resourceSpans'][0]['scopeSpans'][0]['spans'];

foreach ($spans as $span) {
    if ($span['name'] === 'curl_exec') {
        echo "kind: " . ($span['kind'] == 3 ? 'CLIENT' : $span['kind']) . "\n";
        $attrs = [];
        foreach ($span['attributes'] as $attr) {
            $attrs[$attr['key']] = $attr['value']['stringValue'] ?? $attr['value']['intValue'] ?? '';
        }
        echo "has url.full: " . (isset($attrs['url.full']) ? 'yes' : 'no') . "\n";
        echo "has http.request.method: " . (isset($attrs['http.request.method']) ? 'yes' : 'no') . "\n";
        echo "has server.address: " . (isset($attrs['server.address']) ? 'yes' : 'no') . "\n";
        echo "has http.response.status_code: " . (isset($attrs['http.response.status_code']) ? 'yes' : 'no') . "\n";
    }
}
?>
--EXPECT--
kind: CLIENT
has url.full: yes
has http.request.method: yes
has server.address: yes
has http.response.status_code: yes
