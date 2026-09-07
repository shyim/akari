--TEST--
Compaction remaps suspended Fiber stacks and inherited parents
--SKIPIF--
<?php include __DIR__ . '/_skipif.inc'; ?>
--INI--
akari.enable=1
akari.flush_threshold=1
akari.udp_port=14398
--FILE--
<?php
require __DIR__ . '/_msgpack_decode.inc';
$socket = stream_socket_server('udp://127.0.0.1:14398', $errno, $errstr, STREAM_SERVER_BIND);
#[Akari\Span(name: 'worker')]
function worker() { Fiber::suspend(); usleep(1000); }
#[Akari\Span(name: 'dropped', minDurationMs: 10000)]
function startWorker(Fiber $fiber) { $fiber->start(); }
$fiber = new Fiber('worker');
startWorker($fiber); // Drops index 0 while the worker at index 1 is suspended.
usleep(1000);        // Flush/compact again on the main context.
$fiber->resume();
Akari\disable();
$spans = [];
while (true) {
    $r = [$socket]; $w = $e = [];
    if (!stream_select($r, $w, $e, 0, 200000)) break;
    array_push($spans, ...akari_msgpack_decode(stream_socket_recvfrom($socket, 65535))['sp']);
}
fclose($socket);
$root = array_values(array_filter($spans, fn($s) => isset($s['pv'])))[0];
$worker = array_values(array_filter($spans, fn($s) => $s['n'] === 'worker'))[0];
echo 'worker attached to root: ', $worker['p'] === $root['s'] ? 'yes' : 'no', "\n";
$parents = array_column(array_filter($spans, fn($s) => $s['n'] === 'usleep'), 'p');
sort($parents);
$expected = [$root['s'], $worker['s']]; sort($expected);
echo 'sleep parents: ', $parents === $expected ? 'correct' : 'wrong', "\n";
echo 'dropped absent: ', !in_array('dropped', array_column($spans, 'n')) ? 'yes' : 'no', "\n";
?>
--EXPECT--
worker attached to root: yes
sleep parents: correct
dropped absent: yes
