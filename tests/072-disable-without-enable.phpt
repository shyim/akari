--TEST--
otel_profiler_disable without prior enable does not crash
--SKIPIF--
<?php include __DIR__ . '/_skipif.inc'; ?>
--INI--
akari.enable=0
--FILE--
<?php
Akari\disable();
echo "ok\n";
?>
--EXPECT--
ok
