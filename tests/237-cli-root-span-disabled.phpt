--TEST--
CLI: no automatic root span when akari.trace_cli is disabled
--SKIPIF--
<?php include __DIR__ . '/_skipif.inc'; ?>
--INI--
akari.enable=1
akari.trace_cli=0
--FILE--
<?php
// With auto CLI tracing off, the root span stays inactive: with no child span
// on the stack there is no parent to advertise, so the headers are empty.
$headers = Akari\generateDistributedTracingHeaders();
echo 'traceparent present: ' . (isset($headers['traceparent']) ? 'yes' : 'no') . "\n";

// markAsWebTransaction() still promotes the inactive CLI root into a real one.
Akari\markAsWebTransaction();
$headers = Akari\generateDistributedTracingHeaders();
echo 'traceparent after promote: ' . (isset($headers['traceparent']) ? 'yes' : 'no') . "\n";
?>
--EXPECT--
traceparent present: no
traceparent after promote: yes
