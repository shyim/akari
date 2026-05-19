--TEST--
Registry: userland method hooks create spans even with trace_functions=0
--SKIPIF--
<?php include __DIR__ . '/_skipif.inc'; ?>
--INI--
akari.enable=1
akari.trace_functions=0
--FILE--
<?php
// With trace_functions=0, normal user functions should NOT be traced
function normalFunc() { return 1; }
normalFunc();

$count = Akari\getSpanCount();
echo "spans after normalFunc: " . ($count == 0 ? '0' : $count) . "\n";

// I/O hooks should still work
usleep(1000);

$json = Akari\getSpansJson();
$data = json_decode($json, true);
$spans = $data['resourceSpans'][0]['scopeSpans'][0]['spans'] ?? [];
$names = array_column($spans, 'name');
echo "has normalFunc: " . (in_array('normalFunc', $names) ? 'yes' : 'no') . "\n";
echo "has usleep: " . (in_array('usleep', $names) ? 'yes' : 'no') . "\n";
?>
--EXPECT--
spans after normalFunc: 0
has normalFunc: no
has usleep: yes
