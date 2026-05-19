--TEST--
Tracing a simple function produces at least 1 span
--SKIPIF--
<?php include __DIR__ . '/_skipif.inc'; ?>
--INI--
akari.enable=1
akari.trace_functions=1
--FILE--
<?php
function hello() {
    return 42;
}

hello();

$count = Akari\getSpanCount();
echo ($count >= 1) ? "OK: spans >= 1\n" : "FAIL: span_count = $count\n";
?>
--EXPECT--
OK: spans >= 1
