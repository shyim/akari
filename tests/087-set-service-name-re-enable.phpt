--TEST--
setServiceName after disable is picked up on re-enable
--SKIPIF--
<?php include __DIR__ . '/_skipif.inc'; ?>
--INI--
akari.enable=0
akari.trace_functions=1
akari.service_name=ini-default
--FILE--
<?php
// First batch: enable, work, set name, disable
Akari\enable();
function batch1() { usleep(1); }
batch1();
Akari\setServiceName("first-batch");
$json1 = Akari\getSpansJson();
$data1 = json_decode($json1, true);
$svc1 = $data1['resourceSpans'][0]['resource']['attributes'][0]['value']['stringValue'] ?? '';
echo "batch1: " . $svc1 . "\n";

Akari\disable();

// After disable, set a new name for the next batch
Akari\setServiceName("second-batch");

// Re-enable: second batch should use the new name
Akari\enable();
function batch2() { usleep(1); }
batch2();
$json2 = Akari\getSpansJson();
$data2 = json_decode($json2, true);
$svc2 = $data2['resourceSpans'][0]['resource']['attributes'][0]['value']['stringValue'] ?? '';
echo "batch2: " . $svc2 . "\n";
?>
--EXPECT--
batch1: first-batch
batch2: second-batch
