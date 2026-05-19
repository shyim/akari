--TEST--
max_depth=3 limits spans to 3 on a 5-deep call chain
--SKIPIF--
<?php include __DIR__ . '/_skipif.inc'; ?>
--INI--
akari.enable=1
akari.trace_functions=1
akari.max_depth=3
--FILE--
<?php
function level5() { usleep(1); }
function level4() { level5(); }
function level3() { level4(); }
function level2() { level3(); }
function level1() { level2(); }

level1();

$count = Akari\getSpanCount();
echo "spans: $count\n";
?>
--EXPECT--
spans: 3
