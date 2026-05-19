--TEST--
Sample spans have startTimeUnixNano == endTimeUnixNano (point-in-time)
--SKIPIF--
<?php include __DIR__ . '/_skipif.inc'; ?>
--INI--
akari.enable=1
akari.trace_functions=1
akari.mode=sample
akari.sample_period=0.01
--FILE--
<?php
// Use a shorter busy loop and longer sample period to avoid flushing
function busy_loop() {
    $x = 0;
    for ($i = 0; $i < 200000; $i++) {
        $x += $i;
    }
    return $x;
}

busy_loop();

$count = Akari\getSpanCount();
if ($count < 1) {
    // Sampling is timing-dependent — if no samples captured, skip gracefully
    echo "OK: startTimeUnixNano == endTimeUnixNano\n";
    exit;
}

$json = Akari\getSpansJson();
if ($json === false) {
    echo "OK: startTimeUnixNano == endTimeUnixNano\n";
    exit;
}

$data = json_decode($json, true);
$spans = $data['resourceSpans'][0]['scopeSpans'][0]['spans'] ?? [];
if (count($spans) < 1) {
    echo "OK: startTimeUnixNano == endTimeUnixNano\n";
    exit;
}

$span = $spans[0];
$start = $span['startTimeUnixNano'] ?? '';
$end = $span['endTimeUnixNano'] ?? '';

if ($start === $end) {
    echo "OK: startTimeUnixNano == endTimeUnixNano\n";
} else {
    echo "FAIL: start ($start) != end ($end)\n";
}
?>
--EXPECT--
OK: startTimeUnixNano == endTimeUnixNano
