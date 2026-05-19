--TEST--
Closure inside class method shows ClassName::{closure in name
--SKIPIF--
<?php include __DIR__ . '/_skipif.inc'; ?>
--INI--
akari.enable=1
akari.trace_functions=1
--FILE--
<?php
class Runner {
    public function run() {
        $fn = function() {
            return 42;
        };
        return $fn();
    }
}

$r = new Runner();
$r->run();

$json = Akari\getSpansJson();
$data = json_decode($json, true);
$spans = $data['resourceSpans'][0]['scopeSpans'][0]['spans'];

$found = false;
foreach ($spans as $span) {
    if (strpos($span['name'], 'Runner::{closure') !== false) {
        $found = true;
        echo "OK: " . $span['name'] . "\n";
        break;
    }
}

if (!$found) {
    echo "FAIL: Runner::{closure not found in span names\n";
    foreach ($spans as $span) {
        echo "  span: " . $span['name'] . "\n";
    }
}
?>
--EXPECTF--
OK: Runner::{closure:%s(%d)}
