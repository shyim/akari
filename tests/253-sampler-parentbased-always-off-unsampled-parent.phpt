--TEST--
Sampler: parentbased_always_off drops when the parent is not sampled (trace-flags 00)
--SKIPIF--
<?php include __DIR__ . '/_skipif.inc'; ?>
--ENV--
TRACEPARENT=00-11111111111111111111111111111111-2222222222222222-00
--INI--
akari.enable=1
akari.traces_sampler=parentbased_always_off
--FILE--
<?php
usleep(1);

echo 'spans: ' . (Akari\getSpansJson() === false ? 'none' : 'some') . "\n";
?>
--EXPECT--
spans: none
