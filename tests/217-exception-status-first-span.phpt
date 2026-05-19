--TEST--
Exception caught by application code does not mark the span as error
--SKIPIF--
<?php include __DIR__ . '/_skipif.inc'; ?>
--INI--
akari.enable=1
--FILE--
<?php
require __DIR__ . '/_doctrine_mock.inc';

// The exception is thrown while the only span (index 0) is open, but the
// application catches and handles it. The engine hook must NOT mark span 0
// as error and must NOT record an event.
class EM implements \Doctrine\ORM\EntityManagerInterface {
    public function flush() {
        throw new RuntimeException('first span failure');
    }
}

try {
    (new EM())->flush();
} catch (Throwable $e) {
    // handled
}

$json = Akari\getSpansJson();
$data = json_decode($json, true);
$spans = $data['resourceSpans'][0]['scopeSpans'][0]['spans'];
$first = $spans[0] ?? null;

echo 'span 0 status: ' . (($first['status']['code'] ?? null) === 2 ? 'ERROR' : 'not error') . "\n";
echo 'span 0 exception event: ' . (!empty($first['events']) ? 'yes' : 'no') . "\n";
?>
--EXPECT--
span 0 status: not error
span 0 exception event: no
