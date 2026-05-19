--TEST--
file_get_contents with HTTP URL creates CLIENT span with url.full attribute
--SKIPIF--
<?php
include __DIR__ . '/_skipif.inc';
if (!ini_get('allow_url_fopen')) die('skip allow_url_fopen disabled');
?>
--INI--
akari.enable=1
akari.trace_functions=0
--FILE--
<?php
// Use a URL that will likely fail quickly but still create a span
@file_get_contents("http://127.0.0.1:1/nonexistent");

$json = Akari\getSpansJson();
$data = json_decode($json, true);
$spans = $data['resourceSpans'][0]['scopeSpans'][0]['spans'];

foreach ($spans as $span) {
    if ($span['name'] === 'file_get_contents') {
        echo "found: yes\n";
        echo "kind: " . ($span['kind'] == 3 ? 'CLIENT' : $span['kind']) . "\n";
        $attrs = [];
        foreach ($span['attributes'] as $attr) {
            $key = $attr['key'];
            $val = $attr['value']['stringValue'] ?? $attr['value']['intValue'] ?? '';
            $attrs[$key] = $val;
        }
        echo "has url.full: " . (isset($attrs['url.full']) ? 'yes' : 'no') . "\n";
        echo "has http.request.method: " . (isset($attrs['http.request.method']) ? 'yes' : 'no') . "\n";
        echo "has server.address: " . (isset($attrs['server.address']) ? 'yes' : 'no') . "\n";
    }
}
?>
--EXPECT--
found: yes
kind: CLIENT
has url.full: yes
has http.request.method: yes
has server.address: yes
