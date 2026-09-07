--TEST--
fopen and file_put_contents with HTTP URLs create HTTP CLIENT spans
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
@fopen('http://127.0.0.1:1/read', 'r');
@file_put_contents('http://127.0.0.1:1/write', 'payload');

$data = json_decode(Akari\getSpansJson(), true);
$spans = $data['resourceSpans'][0]['scopeSpans'][0]['spans'] ?? [];

foreach (['fopen' => 'GET', 'file_put_contents' => 'PUT'] as $name => $method) {
    $matched = null;
    foreach ($spans as $span) {
        if ($span['name'] === $name) {
            $matched = $span;
            break;
        }
    }

    $attrs = [];
    foreach ($matched['attributes'] ?? [] as $attr) {
        $attrs[$attr['key']] = $attr['value']['stringValue']
            ?? $attr['value']['intValue']
            ?? null;
    }

    echo $name . ' found: ' . ($matched ? 'yes' : 'no') . "\n";
    echo $name . ' kind: ' . (($matched['kind'] ?? null) === 3 ? 'CLIENT' : 'other') . "\n";
    echo $name . ' url: ' . (isset($attrs['url.full']) ? 'yes' : 'no') . "\n";
    echo $name . ' method: '
        . (($attrs['http.request.method'] ?? null) === $method ? 'yes' : 'no')
        . "\n";
    echo $name . ' server: '
        . (($attrs['server.address'] ?? null) === '127.0.0.1' ? 'yes' : 'no')
        . "\n";
}
?>
--EXPECT--
fopen found: yes
fopen kind: CLIENT
fopen url: yes
fopen method: yes
fopen server: yes
file_put_contents found: yes
file_put_contents kind: CLIENT
file_put_contents url: yes
file_put_contents method: yes
file_put_contents server: yes
