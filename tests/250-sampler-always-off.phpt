--TEST--
Sampler: always_off drops every request
--SKIPIF--
<?php include __DIR__ . '/_skipif.inc'; ?>
--INI--
akari.enable=1
akari.traces_sampler=always_off
--FILE--
<?php
usleep(1);

echo 'spans: ' . (Akari\getSpansJson() === false ? 'none' : 'some') . "\n";

$headers = Akari\generateDistributedTracingHeaders();
echo 'traceparent present: ' . (isset($headers['traceparent']) ? 'yes' : 'no') . "\n";
?>
--EXPECT--
spans: none
traceparent present: no
