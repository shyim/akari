--TEST--
Nested calls set parentSpanId on inner span
--SKIPIF--
<?php include __DIR__ . '/_skipif.inc'; ?>
--INI--
akari.enable=1
akari.trace_functions=1
--FILE--
<?php
function inner() {
    return 1;
}

function outer() {
    return inner();
}

outer();

$json = Akari\getSpansJson();
$data = json_decode($json, true);

$found_parent = false;
foreach ($data['resourceSpans'][0]['scopeSpans'][0]['spans'] as $span) {
    if (strpos($span['name'], 'inner') !== false && !empty($span['parentSpanId'])) {
        $found_parent = true;
        break;
    }
}

echo $found_parent ? "OK: inner has parentSpanId\n" : "FAIL: no parentSpanId on inner\n";
?>
--EXPECT--
OK: inner has parentSpanId
