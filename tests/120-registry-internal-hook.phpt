--TEST--
Registry: internal function hooks create spans (sleep)
--SKIPIF--
<?php include __DIR__ . '/_skipif.inc'; ?>
--INI--
akari.enable=1
akari.trace_functions=0
--FILE--
<?php
usleep(1000);

$json = Akari\getSpansJson();
$data = json_decode($json, true);
$spans = $data['resourceSpans'][0]['scopeSpans'][0]['spans'];
$names = array_column($spans, 'name');
echo "has usleep: " . (in_array('usleep', $names) ? 'yes' : 'no') . "\n";
?>
--EXPECT--
has usleep: yes
