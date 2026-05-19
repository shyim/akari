--TEST--
Class method span name contains Class::method
--SKIPIF--
<?php include __DIR__ . '/_skipif.inc'; ?>
--INI--
akari.enable=1
akari.trace_functions=1
--FILE--
<?php
class Greeter {
    public function greet() {
        return 'hello';
    }
}

$g = new Greeter();
$g->greet();

$json = Akari\getSpansJson();
if ($json === false) {
    echo "FAIL: no JSON\n";
} elseif (strpos($json, 'Greeter::greet') !== false) {
    echo "OK: found Greeter::greet\n";
} else {
    echo "FAIL: Greeter::greet not found in JSON\n";
}
?>
--EXPECT--
OK: found Greeter::greet
