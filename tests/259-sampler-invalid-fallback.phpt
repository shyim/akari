--TEST--
Sampler: an unrecognized sampler name warns and falls back to parentbased_always_on
--SKIPIF--
<?php include __DIR__ . '/_skipif.inc'; ?>
--INI--
akari.enable=1
akari.traces_sampler=jaeger_remote
--FILE--
<?php
usleep(1);

echo 'spans: ' . (Akari\getSpansJson() === false ? 'none' : 'some') . "\n";
?>
--EXPECTF--
Warning: PHP Request Startup: unsupported traces sampler "jaeger_remote", falling back to parentbased_always_on in %s on line %d
spans: some
