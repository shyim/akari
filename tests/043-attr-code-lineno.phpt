--TEST--
Span has code.lineno attribute greater than zero
--SKIPIF--
<?php include __DIR__ . '/_skipif.inc'; ?>
--INI--
akari.enable=1
akari.trace_functions=1
--FILE--
<?php
function greet() {
    usleep(1);
}
greet();

$json = Akari\getSpansJson();
$data = json_decode($json, true);
$spans = $data['resourceSpans'][0]['scopeSpans'][0]['spans'];

$found = false;
foreach ($spans as $span) {
    foreach ($span['attributes'] as $attr) {
        if ($attr['key'] === 'code.lineno') {
            $val = $attr['value']['intValue'] ?? $attr['value']['stringValue'] ?? 0;
            if ((int)$val > 0) {
                $found = true;
                break 2;
            }
        }
    }
}

echo $found ? 'ok' : 'not found';
?>
--EXPECT--
ok
