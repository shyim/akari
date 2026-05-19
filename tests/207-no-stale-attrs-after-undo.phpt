--TEST--
Threshold-dropped span attrs do not leak to later spans
--SKIPIF--
<?php include __DIR__ . '/_skipif.inc'; ?>
--INI--
akari.enable=1
akari.trace_functions=0
--FILE--
<?php
// preg_match has a 1ms threshold hook — fast regex creates and undoes a span.
// Without attr cleanup on undo, usleep would inherit "regex" db.system.
preg_match("/x/", "x");
usleep(1000);

$json = Akari\getSpansJson();
$data = json_decode($json, true);
$spans = $data['resourceSpans'][0]['scopeSpans'][0]['spans'] ?? [];

$found_regex = false;
$found_sleep = false;
foreach ($spans as $span) {
    $name = $span['name'] ?? '';
    foreach ($span['attributes'] ?? [] as $attr) {
        if ($attr['key'] === 'db.system') {
            if ($attr['value']['stringValue'] === 'regex') $found_regex = true;
            if ($attr['value']['stringValue'] === 'sleep') $found_sleep = true;
        }
    }
}

if ($found_regex) {
    echo "FAIL: stale regex attribute leaked\n";
} elseif ($found_sleep) {
    echo "OK: no stale attribute leak\n";
} else {
    echo "OK: no stale attribute leak\n";
}
?>
--EXPECT--
OK: no stale attribute leak
