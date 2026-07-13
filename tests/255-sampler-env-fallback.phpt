--TEST--
Sampler: OTEL_TRACES_SAMPLER env is used when akari.traces_sampler is unset
--SKIPIF--
<?php include __DIR__ . '/_skipif.inc'; ?>
--ENV--
OTEL_TRACES_SAMPLER=always_off
--INI--
akari.enable=1
--FILE--
<?php
usleep(1);

echo 'spans: ' . (Akari\getSpansJson() === false ? 'none' : 'some') . "\n";
?>
--EXPECT--
spans: none
