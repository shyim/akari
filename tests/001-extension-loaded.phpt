--TEST--
Check that profiler_otel extension is loaded
--SKIPIF--
<?php include __DIR__ . '/_skipif.inc'; ?>
--FILE--
<?php
var_dump(extension_loaded('akari'));
?>
--EXPECT--
bool(true)
