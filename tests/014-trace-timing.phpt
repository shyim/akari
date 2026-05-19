--TEST--
Span endTimeUnixNano >= startTimeUnixNano
--SKIPIF--
<?php include __DIR__ . '/_skipif.inc'; ?>
--INI--
akari.enable=1
akari.trace_functions=1
--FILE--
<?php
function work() {
    $x = 0;
    for ($i = 0; $i < 1000; $i++) $x += $i;
    return $x;
}

work();

$json = Akari\getSpansJson();
$data = json_decode($json, true);
$spans = $data['resourceSpans'][0]['scopeSpans'][0]['spans'];

$ok = true;
foreach ($spans as $span) {
    // Skip file-level span (still executing, end_time not set yet)
    if ($span['name'] !== 'work') continue;
    $start = $span['startTimeUnixNano'];
    $end = $span['endTimeUnixNano'];
    // Both are numeric strings; pad for safe comparison
    $maxlen = max(strlen($start), strlen($end));
    $s = str_pad($start, $maxlen, '0', STR_PAD_LEFT);
    $e = str_pad($end, $maxlen, '0', STR_PAD_LEFT);
    if ($e < $s) {
        $ok = false;
        echo "FAIL: end ($end) < start ($start)\n";
    }
}

echo $ok ? "OK\n" : "FAIL\n";
?>
--EXPECT--
OK
