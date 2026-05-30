--TEST--
CLI: a root span is created automatically when akari.trace_cli is enabled
--SKIPIF--
<?php include __DIR__ . '/_skipif.inc'; ?>
--INI--
akari.enable=1
akari.trace_cli=1
--FILE--
<?php
// Under CLI with auto tracing on, init_root_span creates an active INTERNAL
// root span. A request-scope log (no child span on the stack) correlates to it,
// and the root span_id is advertised in the distributed-tracing headers.
Akari\log('info', 'cli scope');

$logs = json_decode(Akari\getLogsJson(), true);
$rec = $logs['resourceLogs'][0]['scopeLogs'][0]['logRecords'][0];
echo 'log spanId present: ' . (isset($rec['spanId']) ? 'yes' : 'no') . "\n";
echo 'log traceId length: ' . strlen($rec['traceId']) . "\n";

$headers = Akari\generateDistributedTracingHeaders();
echo 'traceparent present: ' . (isset($headers['traceparent']) ? 'yes' : 'no') . "\n";

// The log span_id must match the active root advertised in the traceparent.
$parts = explode('-', $headers['traceparent']);
echo 'log spanId matches root: ' . ($rec['spanId'] === $parts[2] ? 'yes' : 'no') . "\n";
?>
--EXPECT--
log spanId present: yes
log traceId length: 32
traceparent present: yes
log spanId matches root: yes
