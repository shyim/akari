--TEST--
trace_internal=1 traces internal functions like strtoupper
--SKIPIF--
<?php include __DIR__ . '/_skipif.inc'; ?>
--INI--
akari.enable=1
akari.trace_functions=1
akari.trace_internal=1
--FILE--
<?php
function myUserFunc() {
    strtoupper("hello");
}
myUserFunc();

$json = Akari\getSpansJson();
$data = json_decode($json, true);
$spans = $data['resourceSpans'][0]['scopeSpans'][0]['spans'];
$names = array_column($spans, 'name');
echo "has myUserFunc: " . (in_array('myUserFunc', $names) ? 'yes' : 'no') . "\n";
echo "has strtoupper: " . (in_array('strtoupper', $names) ? 'yes' : 'no') . "\n";
echo "span_count > 2: " . (count($spans) > 2 ? 'yes' : 'no') . "\n";
?>
--EXPECT--
has myUserFunc: yes
has strtoupper: yes
span_count > 2: yes
