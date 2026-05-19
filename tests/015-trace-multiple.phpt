--TEST--
Multiple functions each produce spans, total >= 3
--SKIPIF--
<?php include __DIR__ . '/_skipif.inc'; ?>
--INI--
akari.enable=1
akari.trace_functions=1
--FILE--
<?php
function a() { return 1; }
function b() { return 2; }
function c() { return 3; }

a();
b();
c();

$count = Akari\getSpanCount();
echo ($count >= 3) ? "OK: spans >= 3\n" : "FAIL: span_count = $count\n";
?>
--EXPECT--
OK: spans >= 3
