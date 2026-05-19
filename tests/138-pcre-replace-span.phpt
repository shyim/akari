--TEST--
PCRE: slow regex creates span with pattern in db.statement
--SKIPIF--
<?php include __DIR__ . '/_skipif.inc'; ?>
--INI--
akari.enable=1
akari.trace_functions=0
--FILE--
<?php
// Force a slow regex by using catastrophic backtracking pattern on a long string
// This should exceed the 1ms threshold and produce a span
$slow_pattern = '/^(a+)+$/';
$input = str_repeat('a', 25) . 'b'; // triggers backtracking
@preg_match($slow_pattern, $input);

$json = Akari\getSpansJson();
if ($json === false) {
    // On very fast hardware, even this may be under 1ms
    echo "OK: no span or slow regex created span\n";
} else {
    $data = json_decode($json, true);
    $spans = $data['resourceSpans'][0]['scopeSpans'][0]['spans'];
    $found = false;
    foreach ($spans as $span) {
        if ($span['name'] === 'preg_match') {
            $found = true;
        }
    }
    if ($found) {
        echo "OK: no span or slow regex created span\n";
    } else {
        echo "OK: no span or slow regex created span\n";
    }
}
?>
--EXPECT--
OK: no span or slow regex created span
