--TEST--
enable=1 with no user functions does not crash
--SKIPIF--
<?php include __DIR__ . '/_skipif.inc'; ?>
--INI--
akari.enable=1
--FILE--
<?php
echo "hello\n";
?>
--EXPECT--
hello
