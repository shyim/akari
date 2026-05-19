--TEST--
Span has code.function attribute matching the hooked method name
--SKIPIF--
<?php include __DIR__ . '/_skipif.inc'; ?>
--INI--
akari.enable=1
--FILE--
<?php
require __DIR__ . '/_doctrine_mock.inc';

class MyManager implements \Doctrine\ORM\EntityManagerInterface {
    public function flush() {
        usleep(1);
    }
}

(new MyManager())->flush();

$json = Akari\getSpansJson();
$data = json_decode($json, true);
$spans = $data['resourceSpans'][0]['scopeSpans'][0]['spans'];

$found = false;
foreach ($spans as $span) {
    foreach ($span['attributes'] as $attr) {
        if ($attr['key'] === 'code.function' && $attr['value']['stringValue'] === 'flush') {
            $found = true;
            break 2;
        }
    }
}

echo $found ? 'ok' : 'not found';
?>
--EXPECT--
ok
