--TEST--
Sampler: traceidratio samples everything at 1.0 (INI arg)
--SKIPIF--
<?php include __DIR__ . '/_skipif.inc'; ?>
--INI--
akari.enable=1
akari.traces_sampler=traceidratio
akari.traces_sampler_arg=1.0
--FILE--
<?php
usleep(1);

echo 'spans: ' . (Akari\getSpansJson() === false ? 'none' : 'some') . "\n";
?>
--EXPECT--
spans: some
