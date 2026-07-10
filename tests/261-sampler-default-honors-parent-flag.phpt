--TEST--
Sampler: the default (parentbased_always_on) drops a request whose parent is not sampled
--SKIPIF--
<?php include __DIR__ . '/_skipif.inc'; ?>
--ENV--
TRACEPARENT=00-11111111111111111111111111111111-2222222222222222-00
--INI--
akari.enable=1
--FILE--
<?php
// No sampler configured: the parentbased_always_on default follows the
// incoming sampled flag, so trace-flags 00 means the request is dropped.
usleep(1);

echo 'spans: ' . (Akari\getSpansJson() === false ? 'none' : 'some') . "\n";
?>
--EXPECT--
spans: none
