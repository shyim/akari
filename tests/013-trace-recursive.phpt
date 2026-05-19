--TEST--
Recursive function produces >= 4 spans for recurse(3)
--SKIPIF--
<?php include __DIR__ . '/_skipif.inc'; ?>
--INI--
akari.enable=1
akari.trace_functions=1
--FILE--
<?php
function recurse($n) {
    if ($n <= 0) return;
    recurse($n - 1);
}

recurse(3);

$count = Akari\getSpanCount();
echo ($count >= 4) ? "OK: spans >= 4\n" : "FAIL: span_count = $count\n";
?>
--EXPECT--
OK: spans >= 4
