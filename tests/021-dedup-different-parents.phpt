--TEST--
Helper called from different parents produces frames for both contexts
--SKIPIF--
<?php include __DIR__ . '/_skipif.inc'; ?>
--INI--
akari.enable=1
akari.trace_functions=1
--FILE--
<?php
function helper() {
    return 1;
}

function a() {
    return helper();
}

function b() {
    return helper();
}

a();
b();

$json = Akari\getSpansJson();
$data = json_decode($json, true);
$spans = $data['resourceSpans'][0]['scopeSpans'][0]['spans'];

$helper_spans = 0;
foreach ($spans as $span) {
    if (strpos($span['name'], 'helper') !== false) {
        $helper_spans++;
    }
}

echo ($helper_spans >= 2) ? "OK: helper appears in both contexts\n" : "FAIL: helper_spans = $helper_spans\n";
?>
--EXPECT--
OK: helper appears in both contexts
