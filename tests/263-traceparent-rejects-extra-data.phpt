--TEST--
CLI: traceparent version 00 rejects trailing data
--SKIPIF--
<?php include __DIR__ . '/_skipif.inc'; ?>
--ENV--
TRACEPARENT=00-11111111111111111111111111111111-2222222222222222-01-extra
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
