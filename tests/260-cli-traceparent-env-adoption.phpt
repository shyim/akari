--TEST--
CLI: TRACEPARENT env is adopted as trace context (root joins the caller's trace)
--SKIPIF--
<?php include __DIR__ . '/_skipif.inc'; ?>
--ENV--
TRACEPARENT=00-aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa-bbbbbbbbbbbbbbbb-01
--INI--
akari.enable=1
akari.trace_cli=1
--FILE--
<?php
// Default sampler (always_on): the request is traced regardless of the parent
// flag, but the trace id and parent span id from TRACEPARENT are adopted so
// the CLI run links into the calling trace.
$headers = Akari\generateDistributedTracingHeaders();
$parts = explode('-', $headers['traceparent']);
echo 'traceId adopted: ' . ($parts[1] === 'aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa' ? 'yes' : 'no') . "\n";

Akari\disable(); // finalize + export so the root span is serializable
?>
--EXPECT--
traceId adopted: yes
