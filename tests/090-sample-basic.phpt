--TEST--
Sampling mode captures at least 1 sample
--SKIPIF--
<?php include __DIR__ . '/_skipif.inc'; ?>
--INI--
akari.enable=1
akari.trace_functions=1
akari.mode=sample
akari.sample_period=0.00001
--FILE--
<?php
function busy_work() {
    $x = 0;
    for ($i = 0; $i < 2000000; $i++) {
        $x += $i;
    }
    return $x;
}

busy_work();
usleep(50000);

$count = Akari\getSpanCount();
echo ($count >= 1) ? "OK: sample spans >= 1\n" : "FAIL: span_count = $count\n";
?>
--EXPECT--
OK: sample spans >= 1
