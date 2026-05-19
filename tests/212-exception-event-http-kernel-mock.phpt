--TEST--
Exception event: HttpKernel::handleThrowable records exception event
--SKIPIF--
<?php include __DIR__ . '/_skipif.inc'; ?>
--INI--
akari.enable=1
akari.trace_functions=0
--FILE--
<?php
namespace Symfony\Component\HttpKernel {
    class HttpKernel {
        public function handleThrowable(\Throwable $e, $request = null): void {
            // Mock
        }
    }
}

namespace {
    $kernel = new \Symfony\Component\HttpKernel\HttpKernel();
    $kernel->handleThrowable(new \InvalidArgumentException("Bad input"));

    $json = \Akari\getSpansJson();
    $data = json_decode($json, true);
    $spans = $data['resourceSpans'][0]['scopeSpans'][0]['spans'];

    foreach ($spans as $span) {
        if (strpos($span['name'], 'handleThrowable') !== false) {
            echo "found: yes\n";
            echo "status: " . ($span['status']['code'] == 2 ? 'ERROR' : $span['status']['code']) . "\n";
            if (isset($span['events'][0])) {
                $ev = $span['events'][0];
                foreach ($ev['attributes'] as $attr) {
                    if ($attr['key'] === 'exception.type') echo "type: " . $attr['value']['stringValue'] . "\n";
                    if ($attr['key'] === 'exception.message') echo "message: " . $attr['value']['stringValue'] . "\n";
                }
            }
        }
    }
}
?>
--EXPECT--
found: yes
status: ERROR
type: InvalidArgumentException
message: Bad input
