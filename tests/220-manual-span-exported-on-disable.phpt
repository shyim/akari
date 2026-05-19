--TEST--
createSpan is finalized before disable export
--SKIPIF--
<?php
include __DIR__ . '/_skipif.inc';
$sock = @stream_socket_server('udp://127.0.0.1:54329', $errno, $errstr, STREAM_SERVER_BIND);
if (!$sock) die('skip udp port unavailable');
fclose($sock);
?>
--INI--
akari.enable=1
akari.trace_functions=0
akari.udp_host=127.0.0.1
akari.udp_port=54329
--FILE--
<?php
$sock = stream_socket_server('udp://127.0.0.1:54329', $errno, $errstr, STREAM_SERVER_BIND);
if (!$sock) {
    echo "received datagram: no\n";
    echo "manual span exported: no\n";
    exit;
}

stream_set_blocking($sock, false);

Akari\createSpan('manual-shutdown-span');
Akari\disable();

$read = [$sock];
$write = null;
$except = null;
$ready = stream_select($read, $write, $except, 2, 0);
$datagram = $ready ? stream_socket_recvfrom($sock, 8192) : '';
fclose($sock);

echo 'received datagram: ' . ($datagram !== '' ? 'yes' : 'no') . "\n";
echo 'manual span exported: ' . (strpos($datagram, 'manual-shutdown-span') !== false ? 'yes' : 'no') . "\n";
?>
--EXPECT--
received datagram: yes
manual span exported: yes
