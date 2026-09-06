--TEST--
CLI: traceparent rejects uppercase hexadecimal characters
--SKIPIF--
<?php include __DIR__ . '/_skipif.inc'; ?>
--ENV--
TRACEPARENT=00-AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA-bbbbbbbbbbbbbbbb-01
--INI--
akari.enable=1
akari.traces_sampler=parentbased_always_off
--FILE--
<?php
usleep(1);
echo Akari\getSpansJson() === false ? "rejected\n" : "accepted\n";
?>
--EXPECT--
rejected
