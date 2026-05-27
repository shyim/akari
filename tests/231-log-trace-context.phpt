--TEST--
Akari\log records carry trace_id and the current/root span_id for correlation
--SKIPIF--
<?php include __DIR__ . '/_skipif.inc'; ?>
--INI--
akari.enable=1
--FILE--
<?php
// Web transaction: an active root span exists, so a request-scope log correlates
// to the root span_id (there is no hooked child span on the stack here).
Akari\markAsWebTransaction();
Akari\log('info', 'request scope');

$data = json_decode(Akari\getLogsJson(), true);
$rec = $data['resourceLogs'][0]['scopeLogs'][0]['logRecords'][0];

echo 'traceId length: ' . strlen($rec['traceId']) . "\n";
echo 'spanId present: ' . (isset($rec['spanId']) ? 'yes' : 'no') . "\n";
echo 'spanId length: ' . strlen($rec['spanId']) . "\n";

// The log's span_id must match the root span advertised in the traceparent.
$tp = Akari\generateDistributedTracingHeaders()['traceparent'];
// traceparent = 00-<32 trace>-<16 span>-01
$parts = explode('-', $tp);
echo 'spanId matches root: ' . ($rec['spanId'] === $parts[2] ? 'yes' : 'no') . "\n";
echo 'traceId matches: ' . ($rec['traceId'] === $parts[1] ? 'yes' : 'no') . "\n";
?>
--EXPECT--
traceId length: 32
spanId present: yes
spanId length: 16
spanId matches root: yes
traceId matches: yes
