--TEST--
Exception event: an exception caught by application code is NOT recorded
--SKIPIF--
<?php include __DIR__ . '/_skipif.inc'; ?>
--INI--
akari.enable=1
--FILE--
<?php
require __DIR__ . '/_doctrine_mock.inc';

// An exception thrown inside a hooked span but caught and handled by
// application code is NOT an error: it must not be recorded as an event
// and must not flip the span status to error.
class EM implements \Doctrine\ORM\EntityManagerInterface {
    public function flush() {
        throw new \RuntimeException("test error message");
    }
}

try {
    (new EM())->flush();
} catch (\Throwable $e) {
    // handled — the application recovered
}

$json = Akari\getSpansJson();
$data = json_decode($json, true);
$spans = $data['resourceSpans'][0]['scopeSpans'][0]['spans'];
$names = array_column($spans, 'name');

echo "has flush span: " . (in_array('EM::flush', $names) ? 'yes' : 'no') . "\n";

$found = false;
foreach ($spans as $span) {
    foreach ($span['events'] ?? [] as $event) {
        $attr = array_column($event['attributes'] ?? [], 'value', 'key');
        if (($attr['exception.type']['stringValue'] ?? '') === 'RuntimeException'
            && ($attr['exception.message']['stringValue'] ?? '') === 'test error message') {
            $found = true;
        }
    }
}
echo "exception event recorded: " . ($found ? 'yes' : 'no') . "\n";

$flush = null;
foreach ($spans as $span) {
    if ($span['name'] === 'EM::flush') $flush = $span;
}
echo "flush span status: " . (($flush['status']['code'] ?? 0) === 2 ? 'ERROR' : 'not error') . "\n";
?>
--EXPECT--
has flush span: yes
exception event recorded: no
flush span status: not error
