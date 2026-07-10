--TEST--
Sampler: parentbased_always_off drops requests without an incoming trace context
--SKIPIF--
<?php include __DIR__ . '/_skipif.inc'; ?>
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
