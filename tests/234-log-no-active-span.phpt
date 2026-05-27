--TEST--
Akari\log under CLI with no active span omits spanId but keeps trace_id
--SKIPIF--
<?php include __DIR__ . '/_skipif.inc'; ?>
--INI--
akari.enable=1
--FILE--
<?php
// The test SAPI is CLI, so the root span is inactive (init_root_span sets
// root.active = 0 for CLI) and no child span is on the stack. The log record is
// still buffered, carries the trace_id, but has no span_id to correlate to.
Akari\log('info', 'cli scope, no span');

$data = json_decode(Akari\getLogsJson(), true);
$rec = $data['resourceLogs'][0]['scopeLogs'][0]['logRecords'][0];

echo 'record present: ' . (isset($rec) ? 'yes' : 'no') . "\n";
echo 'traceId length: ' . strlen($rec['traceId']) . "\n";
echo 'spanId present: ' . (isset($rec['spanId']) ? 'yes' : 'no') . "\n";
?>
--EXPECT--
record present: yes
traceId length: 32
spanId present: no
