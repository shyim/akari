--TEST--
Span has code.namespace attribute for class method
--SKIPIF--
<?php include __DIR__ . '/_skipif.inc'; ?>
--INI--
akari.enable=1
akari.trace_functions=1
--FILE--
<?php
class MyClass {
    public function doIt() {
        usleep(1);
    }
}

$obj = new MyClass();
$obj->doIt();

$json = Akari\getSpansJson();
$data = json_decode($json, true);
$spans = $data['resourceSpans'][0]['scopeSpans'][0]['spans'];

$found = false;
foreach ($spans as $span) {
    foreach ($span['attributes'] as $attr) {
        if ($attr['key'] === 'code.namespace' && $attr['value']['stringValue'] === 'MyClass') {
            $found = true;
            break 2;
        }
    }
}

echo $found ? 'ok' : 'not found';
?>
--EXPECT--
ok
