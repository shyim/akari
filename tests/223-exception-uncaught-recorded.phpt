--TEST--
Exception event: an uncaught exception IS recorded and marks the span as error
--SKIPIF--
<?php include __DIR__ . '/_skipif.inc'; ?>
--INI--
akari.enable=1
--FILE--
<?php
require __DIR__ . '/_doctrine_mock.inc';

// An exception that escapes all frames (never caught) is a genuine error:
// the engine hook must record it as an event and mark its span as error.
// We inspect the spans from a shutdown function, after the exception has
// propagated out of every frame.
class EM implements \Doctrine\ORM\EntityManagerInterface {
    public function flush() {
        throw new RuntimeException('uncaught failure');
    }
}

register_shutdown_function(function () {
    $json = Akari\getSpansJson();
    $data = json_decode($json, true);
    $spans = $data['resourceSpans'][0]['scopeSpans'][0]['spans'];
    $first = $spans[0] ?? null;

    echo 'span 0 status: ' . (($first['status']['code'] ?? null) === 2 ? 'ERROR' : 'not error') . "\n";

    $found = false;
    foreach ($spans as $span) {
        foreach ($span['events'] ?? [] as $event) {
            $attr = array_column($event['attributes'] ?? [], 'value', 'key');
            if (($attr['exception.type']['stringValue'] ?? '') === 'RuntimeException'
                && ($attr['exception.message']['stringValue'] ?? '') === 'uncaught failure') {
                $found = true;
            }
        }
    }
    echo 'exception event recorded: ' . ($found ? 'yes' : 'no') . "\n";
});

(new EM())->flush();
?>
--EXPECTF--
%AFatal error: Uncaught RuntimeException: uncaught failure%a
span 0 status: ERROR
exception event recorded: yes
