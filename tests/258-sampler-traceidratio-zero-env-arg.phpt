--TEST--
Sampler: traceidratio with ratio 0 (OTEL_TRACES_SAMPLER_ARG env) drops everything
--SKIPIF--
<?php include __DIR__ . '/_skipif.inc'; ?>
--ENV--
OTEL_TRACES_SAMPLER_ARG=0
--INI--
akari.enable=1
akari.traces_sampler=traceidratio
--FILE--
<?php
usleep(1);

echo 'spans: ' . (Akari\getSpansJson() === false ? 'none' : 'some') . "\n";
?>
--EXPECT--
spans: none
