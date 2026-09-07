--TEST--
Manual enable observes functions previously called while disabled across sessions
--SKIPIF--
<?php include __DIR__ . '/_skipif.inc'; ?>
--INI--
akari.enable=0
--FILE--
<?php
#[Akari\Span(name: 'work')]
function work() { usleep(1000); }
function startTracing() { Akari\enable(); work(); }
work();
for ($i = 0; $i < 2; $i++) {
    startTracing();
    $spans = json_decode(Akari\getSpansJson(), true)['resourceSpans'][0]['scopeSpans'][0]['spans'];
    $names = array_column($spans, 'name');
    sort($names);
    echo 'session ', $i, ': ', implode(', ', $names), "\n";
    Akari\disable();
    work();
}
?>
--EXPECT--
session 0: usleep, work
session 1: usleep, work
