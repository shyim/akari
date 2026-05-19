--TEST--
trace_internal=0 does not trace internal functions like strlen
--SKIPIF--
<?php include __DIR__ . '/_skipif.inc'; ?>
--INI--
akari.enable=1
akari.trace_functions=1
akari.trace_internal=0
--FILE--
<?php
function myUserFunc() {
    strlen("hello");
}
myUserFunc();

$json = Akari\getSpansJson();
$data = json_decode($json, true);
$spans = $data['resourceSpans'][0]['scopeSpans'][0]['spans'];
$names = array_column($spans, 'name');
echo "has myUserFunc: " . (in_array('myUserFunc', $names) ? 'yes' : 'no') . "\n";
echo "has strlen: " . (in_array('strlen', $names) ? 'yes' : 'no') . "\n";
?>
--EXPECT--
has myUserFunc: yes
has strlen: no
