--TEST--
Exception event: a caught throw earlier in the SAME open span is dropped, only the later uncaught throw is recorded
--SKIPIF--
<?php include __DIR__ . '/_skipif.inc'; ?>
--INI--
akari.enable=1
--FILE--
<?php
require __DIR__ . '/_doctrine_mock.inc';

// A single hooked span (flush) catches one exception internally, then throws a
// second exception that escapes. The caught one was recorded pending against
// the still-open flush span; because it was caught it must be dropped, and the
// engine may reuse its freed object address for the second throw. Only the
// escaping exception must be recorded and mark the span ERROR.
class CatchThenThrow implements \Doctrine\ORM\EntityManagerInterface {
    public function flush() {
        try {
            throw new RuntimeException('handled and recovered');
        } catch (\Throwable $e) {
            // recovered — drop a reference so the engine can reuse the slot
            unset($e);
        }
        throw new LogicException('escapes every frame');
    }
}

register_shutdown_function(function () {
    $json = Akari\getSpansJson();
    $data = json_decode($json, true);
    $spans = $data['resourceSpans'][0]['scopeSpans'][0]['spans'];

    $messages = [];
    $escapedErr = false;
    foreach ($spans as $span) {
        $err = ($span['status']['code'] ?? 0) === 2;
        foreach ($span['events'] ?? [] as $event) {
            $attr = array_column($event['attributes'] ?? [], 'value', 'key');
            $msg = $attr['exception.message']['stringValue'] ?? '';
            $messages[] = $msg;
            if ($msg === 'escapes every frame') $escapedErr = $err;
        }
    }

    echo 'caught event recorded: ' . (in_array('handled and recovered', $messages, true) ? 'yes' : 'no') . "\n";
    echo 'escaped event recorded: ' . (in_array('escapes every frame', $messages, true) ? 'yes' : 'no') . "\n";
    echo 'escaped span status: ' . ($escapedErr ? 'ERROR' : 'not error') . "\n";
});

(new CatchThenThrow())->flush();
?>
--EXPECTF--
%AFatal error: Uncaught LogicException: escapes every frame%a
caught event recorded: no
escaped event recorded: yes
escaped span status: ERROR
