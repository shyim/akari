--TEST--
file_get_contents with local file does NOT create span (under 10ms threshold)
--SKIPIF--
<?php include __DIR__ . '/_skipif.inc'; ?>
--INI--
akari.enable=1
akari.trace_functions=0
--FILE--
<?php
// Local file reads should be fast and under the 10ms threshold — no span
file_get_contents(__FILE__);

$count = Akari\getSpanCount();
echo "spans for local file read: $count\n";
?>
--EXPECT--
spans for local file read: 0
