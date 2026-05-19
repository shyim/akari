--TEST--
otel_profiler_enable and otel_profiler_disable return true
--SKIPIF--
<?php include __DIR__ . '/_skipif.inc'; ?>
--INI--
akari.enable=0
--FILE--
<?php
$r1 = Akari\enable();
$r2 = Akari\disable();

var_dump($r1);
var_dump($r2);
?>
--EXPECT--
bool(true)
bool(true)
