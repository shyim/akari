--TEST--
With enable=0, span count is 0
--SKIPIF--
<?php include __DIR__ . '/_skipif.inc'; ?>
--INI--
akari.enable=0
--FILE--
<?php
var_dump(Akari\getSpanCount());
?>
--EXPECT--
int(0)
