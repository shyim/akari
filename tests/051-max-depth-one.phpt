--TEST--
max_depth=1 records only the top-level call
--SKIPIF--
<?php include __DIR__ . '/_skipif.inc'; ?>
--INI--
akari.enable=1
akari.trace_functions=1
akari.max_depth=1
--FILE--
<?php
function b() { usleep(1); }
function a() { b(); }

a();

$count = Akari\getSpanCount();
echo "spans: $count\n";
?>
--EXPECT--
spans: 1
