--TEST--
Mid-request export waits for duration decisions on all ancestors
--SKIPIF--
<?php include __DIR__ . '/_skipif.inc'; ?>
--INI--
akari.enable=1
akari.flush_threshold=1
akari.udp_port=14397
--FILE--
<?php
require __DIR__ . '/_msgpack_decode.inc';
$socket = stream_socket_server('udp://127.0.0.1:14397', $errno, $errstr, STREAM_SERVER_BIND);
#[Akari\Span(name: 'inner')]
function innerWork() { usleep(1000); }
#[Akari\Span(name: 'dropped', minDurationMs: 10000)]
function droppedWork() { innerWork(); }
#[Akari\Span(name: 'kept', minDurationMs: 0.001)]
function keptWork() { innerWork(); }
droppedWork();
keptWork();
Akari\disable();
$spans = [];
while (true) {
    $r = [$socket]; $w = $e = [];
    if (!stream_select($r, $w, $e, 0, 200000)) break;
    array_push($spans, ...akari_msgpack_decode(stream_socket_recvfrom($socket, 65535))['sp']);
}
fclose($socket);
$ids = array_column($spans, 's');
$orphans = array_filter($spans, fn($s) => !isset($s['pv']) && !in_array($s['p'], $ids, true));
echo 'orphan count: ', count($orphans), "\n";
$names = array_column($spans, 'n');
echo 'inner count: ', count(array_filter($names, fn($n) => $n === 'inner')), "\n";
echo 'sleep count: ', count(array_filter($names, fn($n) => $n === 'usleep')), "\n";
echo 'kept present: ', in_array('kept', $names) ? 'yes' : 'no', "\n";
echo 'dropped absent: ', !in_array('dropped', $names) ? 'yes' : 'no', "\n";
?>
--EXPECT--
orphan count: 0
inner count: 2
sleep count: 2
kept present: yes
dropped absent: yes
