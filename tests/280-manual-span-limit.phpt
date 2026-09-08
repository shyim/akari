--TEST--
Manual spans are bounded and every accepted span is finalized and exported
--SKIPIF--
<?php include __DIR__ . '/_skipif.inc'; ?>
--INI--
akari.enable=1
akari.trace_cli=0
akari.udp_port=15378
--FILE--
<?php
require __DIR__ . '/_msgpack_decode.inc';
$socket = stream_socket_server('udp://127.0.0.1:15378', $errno, $errstr, STREAM_SERVER_BIND);
if (!$socket) throw new RuntimeException($errstr);
stream_set_timeout($socket, 2);
$accepted = 0;
for ($i = 0; $i < 100000; $i++) {
    if (Akari\createSpan('bounded-manual') !== false) $accepted++;
}
echo "accepted: $accepted\n";
Akari\disable();
$finished = 0;
while ($finished < $accepted) {
    $packet = stream_socket_recvfrom($socket, 65535);
    if ($packet === false || $packet === '') break;
    foreach (akari_msgpack_decode($packet)['sp'] as $span) {
        if ($span['n'] === 'bounded-manual' && $span['te'] >= $span['ts']) $finished++;
    }
}
echo "finished: $finished\n";
fclose($socket);
?>
--EXPECT--
accepted: 32
finished: 32
