--TEST--
CLI: child and manual spans preserve inbound parent when trace_cli is disabled
--SKIPIF--
<?php include __DIR__ . '/_skipif.inc'; ?>
--ENV--
TRACEPARENT=00-11111111111111111111111111111111-2222222222222222-01
--INI--
akari.enable=1
akari.trace_cli=0
akari.trace_functions=0
--FILE--
<?php
usleep(1000);
Akari\createSpan('manual-child');

$data = json_decode(Akari\getSpansJson(), true);
$spans = $data['resourceSpans'][0]['scopeSpans'][0]['spans'] ?? [];

foreach (['usleep', 'manual-child'] as $name) {
    $matched = null;
    foreach ($spans as $span) {
        if ($span['name'] === $name) {
            $matched = $span;
            break;
        }
    }

    echo $name . ' trace: '
        . (($matched['traceId'] ?? null) === '11111111111111111111111111111111' ? 'yes' : 'no')
        . "\n";
    echo $name . ' parent: '
        . (($matched['parentSpanId'] ?? null) === '2222222222222222' ? 'yes' : 'no')
        . "\n";
}
?>
--EXPECT--
usleep trace: yes
usleep parent: yes
manual-child trace: yes
manual-child parent: yes
