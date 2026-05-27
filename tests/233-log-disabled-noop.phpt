--TEST--
Akari\log is a no-op when profiling is disabled
--SKIPIF--
<?php include __DIR__ . '/_skipif.inc'; ?>
--INI--
akari.enable=0
--FILE--
<?php
Akari\log('error', 'should be dropped', ['k' => 'v']);
var_dump(Akari\getLogsJson());
?>
--EXPECT--
bool(false)
