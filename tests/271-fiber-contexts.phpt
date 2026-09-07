--TEST--
Interleaved Fibers keep independent span parents and begin/end pairing
--SKIPIF--
<?php include __DIR__ . '/_skipif.inc'; ?>
--INI--
akari.enable=1
akari.flush_threshold=100000
--FILE--
<?php
#[Akari\Span(name: 'fiber-a')]
function fiberA() { Fiber::suspend(); usleep(1000); }
#[Akari\Span(name: 'fiber-b')]
function fiberB() { Fiber::suspend(); usleep(1000); }
#[Akari\Span(name: 'main-work')]
function mainWork() { usleep(1000); }
$a = new Fiber('fiberA');
$b = new Fiber('fiberB');
$a->start();
$b->start();
mainWork();
$a->resume(); // Finish the first Fiber while the second is still suspended.
$b->resume();
$spans = json_decode(Akari\getSpansJson(), true)['resourceSpans'][0]['scopeSpans'][0]['spans'];
$named = array_column($spans, null, 'name');
$rootId = $named['fiber-a']['parentSpanId'];
echo 'siblings: ', $named['fiber-b']['parentSpanId'] === $rootId && $named['main-work']['parentSpanId'] === $rootId ? 'yes' : 'no', "\n";
$parents = array_column(array_filter($spans, fn($s) => $s['name'] === 'usleep'), 'parentSpanId');
$expected = [$named['fiber-a']['spanId'], $named['fiber-b']['spanId'], $named['main-work']['spanId']];
sort($parents); sort($expected);
echo 'sleep parents: ', $parents === $expected ? 'correct' : 'wrong', "\n";
echo 'all completed: ', count(array_filter($spans, fn($s) => (int)$s['endTimeUnixNano'] > (int)$s['startTimeUnixNano'])) === 6 ? 'yes' : 'no', "\n";
?>
--EXPECT--
siblings: yes
sleep parents: correct
all completed: yes
