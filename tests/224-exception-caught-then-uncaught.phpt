--TEST--
Exception event: a caught exception before an uncaught one is dropped, only the uncaught is recorded
--SKIPIF--
<?php include __DIR__ . '/_skipif.inc'; ?>
--INI--
akari.enable=1
--FILE--
<?php
require __DIR__ . '/_doctrine_mock.inc';

// Two hooked spans throw in one request: the first is caught and handled, the
// second escapes. Only the escaping exception is a genuine error — its span
// must be marked ERROR and its event recorded; the caught one must be dropped.
class Caught implements \Doctrine\ORM\EntityManagerInterface {
    public function flush() { throw new RuntimeException('handled and recovered'); }
}
class Escaped implements \Doctrine\ORM\EntityManagerInterface {
    public function flush() { throw new RuntimeException('escapes every frame'); }
}

register_shutdown_function(function () {
    $json = Akari\getSpansJson();
    $data = json_decode($json, true);
    $spans = $data['resourceSpans'][0]['scopeSpans'][0]['spans'];

    $caughtErr = false;
    $escapedErr = false;
    $messages = [];
    foreach ($spans as $span) {
        $err = ($span['status']['code'] ?? 0) === 2;
        foreach ($span['events'] ?? [] as $event) {
            $attr = array_column($event['attributes'] ?? [], 'value', 'key');
            $msg = $attr['exception.message']['stringValue'] ?? '';
            $messages[] = $msg;
            if ($msg === 'handled and recovered') $caughtErr = $err;
            if ($msg === 'escapes every frame')   $escapedErr = $err;
        }
    }

    echo 'caught event recorded: ' . (in_array('handled and recovered', $messages, true) ? 'yes' : 'no') . "\n";
    echo 'escaped event recorded: ' . (in_array('escapes every frame', $messages, true) ? 'yes' : 'no') . "\n";
    echo 'escaped span status: ' . ($escapedErr ? 'ERROR' : 'not error') . "\n";
});

try {
    (new Caught())->flush();
} catch (\Throwable $e) {
    // handled — recovered
}

(new Escaped())->flush();
?>
--EXPECTF--
%AFatal error: Uncaught RuntimeException: escapes every frame%a
caught event recorded: no
escaped event recorded: yes
escaped span status: ERROR
