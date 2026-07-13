--TEST--
Sampler: akari.traces_sampler INI takes precedence over OTEL_TRACES_SAMPLER env
--SKIPIF--
<?php include __DIR__ . '/_skipif.inc'; ?>
--ENV--
OTEL_TRACES_SAMPLER=always_off
--INI--
akari.enable=1
akari.traces_sampler=always_on
--FILE--
<?php
usleep(1);

echo 'spans: ' . (Akari\getSpansJson() === false ? 'none' : 'some') . "\n";
?>
--EXPECT--
spans: some
