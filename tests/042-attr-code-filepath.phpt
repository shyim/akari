--TEST--
Span has non-empty code.filepath attribute
--SKIPIF--
<?php include __DIR__ . '/_skipif.inc'; ?>
--INI--
akari.enable=1
akari.trace_functions=1
--FILE--
<?php
function hello() {
    usleep(1);
}
hello();

$json = Akari\getSpansJson();
$data = json_decode($json, true);
$spans = $data['resourceSpans'][0]['scopeSpans'][0]['spans'];

$found = false;
foreach ($spans as $span) {
    foreach ($span['attributes'] as $attr) {
        if ($attr['key'] === 'code.filepath' && !empty($attr['value']['stringValue'])) {
            $found = true;
            break 2;
        }
    }
}

echo $found ? 'ok' : 'not found';
?>
--EXPECT--
ok
