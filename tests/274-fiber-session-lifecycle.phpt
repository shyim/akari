--TEST--
Tracing started inside a Fiber survives its destruction and a new session
--SKIPIF--
<?php include __DIR__ . '/_skipif.inc'; ?>
--INI--
akari.enable=0
--FILE--
<?php
#[Akari\Span(name: 'work')]
function work() { usleep(1000); }
$fiber = new Fiber(function () {
    Akari\enable();
    work();
    Fiber::suspend();
});
$fiber->start();
work();
$fiber->resume();
unset($fiber);
for ($i = 0; $i < 3; $i++) {
    $fiber = new Fiber('work');
    $fiber->start();
    unset($fiber);
}
$spans = json_decode(Akari\getSpansJson(), true)['resourceSpans'][0]['scopeSpans'][0]['spans'];
echo 'work count: ', count(array_filter($spans, fn($s) => $s['name'] === 'work')), "\n";
$rootParents = array_unique(array_column(array_filter($spans, fn($s) => $s['name'] === 'work'), 'parentSpanId'));
echo 'shared root: ', count($rootParents) === 1 ? 'yes' : 'no', "\n";
Akari\disable();
Akari\enable();
$fiber = new Fiber('work');
$fiber->start();
unset($fiber);
echo 'new session spans: ', Akari\getSpanCount(), "\n";
Akari\disable();
?>
--EXPECT--
work count: 5
shared root: yes
new session spans: 2
