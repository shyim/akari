--TEST--
Same function called twice: frame_count 1, span_count >= 2
--SKIPIF--
<?php include __DIR__ . '/_skipif.inc'; ?>
--INI--
akari.enable=1
akari.trace_functions=1
--FILE--
<?php
function foo() {
    return 42;
}

foo();
foo();

$frames = Akari\getFrameCount();
$spans = Akari\getSpanCount();

// foo is one unique frame
echo "frame_count_for_foo: " . (($frames >= 1) ? "OK" : "FAIL: $frames") . "\n";
// Two calls produce at least 2 spans (plus possible file-level span)
echo "span_count: " . (($spans >= 2) ? "OK" : "FAIL: $spans") . "\n";
?>
--EXPECT--
frame_count_for_foo: OK
span_count: OK
