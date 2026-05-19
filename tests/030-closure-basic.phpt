--TEST--
Closure span name matches {closure} pattern
--SKIPIF--
<?php include __DIR__ . '/_skipif.inc'; ?>
--INI--
akari.enable=1
akari.trace_functions=1
--FILE--
<?php
$fn = function() {
    return 99;
};

$fn();

$json = Akari\getSpansJson();
$data = json_decode($json, true);
$spans = $data['resourceSpans'][0]['scopeSpans'][0]['spans'];

$found = false;
foreach ($spans as $span) {
    if (preg_match('/\{closure/', $span['name'])) {
        $found = true;
        echo "OK: closure span found: " . $span['name'] . "\n";
        break;
    }
}

if (!$found) {
    echo "FAIL: no closure span found\n";
}
?>
--EXPECTF--
OK: closure span found: {closure:%s(%d)}
