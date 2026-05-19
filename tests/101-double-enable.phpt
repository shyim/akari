--TEST--
Calling otel_profiler_enable twice does not crash
--SKIPIF--
<?php include __DIR__ . '/_skipif.inc'; ?>
--INI--
akari.enable=0
--FILE--
<?php
Akari\enable();
Akari\enable();
echo "ok\n";
Akari\disable();
?>
--EXPECT--
ok
