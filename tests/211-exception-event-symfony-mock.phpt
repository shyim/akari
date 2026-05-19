--TEST--
Exception event: Symfony ErrorHandler::handleException records exception event on span
--SKIPIF--
<?php include __DIR__ . '/_skipif.inc'; ?>
--INI--
akari.enable=1
akari.trace_functions=0
--FILE--
<?php
/*
 * Create a mock Symfony ErrorHandler class to test the hook.
 * The hook matches "Symfony\Component\ErrorHandler\ErrorHandler::handleException"
 * via instanceof — so we need the exact class.
 */
namespace Symfony\Component\ErrorHandler {
    class ErrorHandler {
        public function handleException(\Throwable $exception): void {
            // In real Symfony, this renders an error page.
        }
    }
}

namespace {
    $handler = new \Symfony\Component\ErrorHandler\ErrorHandler();
    $handler->handleException(new \RuntimeException("Something went wrong"));

    $json = \Akari\getSpansJson();
    $data = json_decode($json, true);
    $spans = $data['resourceSpans'][0]['scopeSpans'][0]['spans'];

    $found = false;
    foreach ($spans as $span) {
        $name = $span['name'];
        if (strpos($name, 'handleException') !== false) {
            $found = true;
            echo "found handleException: yes\n";
            echo "status: " . ($span['status']['code'] == 2 ? 'ERROR' : $span['status']['code']) . "\n";

            // Check for exception event
            if (isset($span['events'])) {
                foreach ($span['events'] as $event) {
                    if ($event['name'] === 'exception') {
                        echo "event name: exception\n";
                        foreach ($event['attributes'] as $attr) {
                            if ($attr['key'] === 'exception.type') {
                                echo "exception.type: " . $attr['value']['stringValue'] . "\n";
                            }
                            if ($attr['key'] === 'exception.message') {
                                echo "exception.message: " . $attr['value']['stringValue'] . "\n";
                            }
                        }
                    }
                }
            } else {
                echo "FAIL: no events on span\n";
            }
        }
    }
    if (!$found) echo "FAIL: handleException span not found\n";
}
?>
--EXPECT--
found handleException: yes
status: ERROR
event name: exception
exception.type: RuntimeException
exception.message: Something went wrong
