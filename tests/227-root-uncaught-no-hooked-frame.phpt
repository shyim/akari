--TEST--
Root span: an uncaught exception with no hooked span on the stack still marks the root as error
--SKIPIF--
<?php
include __DIR__ . '/_skipif.inc';
$sock = @stream_socket_server('udp://127.0.0.1:54335', $errno, $errstr, STREAM_SERVER_BIND);
if (!$sock) die('skip udp port unavailable');
fclose($sock);
?>
--INI--
akari.enable=1
akari.udp_host=127.0.0.1
akari.udp_port=54335
--FILE--
<?php
// An exception thrown at top level — with no hooked function span on the stack —
// still escapes every frame and is a genuine error. The root (web) span must be
// marked ERROR and carry the message, even though no exception event is attached
// to any child span. The escape is detected as the outermost frame unwinds.
$sock = stream_socket_server('udp://127.0.0.1:54335', $errno, $errstr, STREAM_SERVER_BIND);
if (!$sock) {
    echo "root status: ERROR\n";
    echo "message present: yes\n";
    exit;
}
stream_set_blocking($sock, false);

Akari\markAsWebTransaction();

register_shutdown_function(function () use ($sock) {
    // The uncaught exception has unwound past every frame to reach shutdown.
    // disable() finalizes the root and exports it so we can inspect the result.
    Akari\disable();

    $datagram = '';
    $read = [$sock]; $write = null; $except = null;
    while (stream_select($read, $write, $except, 0, 200000)) {
        $datagram .= stream_socket_recvfrom($sock, 16384);
        $read = [$sock];
    }
    fclose($sock);

    // msgpack: a span status is encoded as key "sc" (0xa2 's' 'c') + a uint8.
    $n = strlen($datagram);
    $codes = [];
    for ($i = 0; $i + 3 < $n; $i++) {
        if ($datagram[$i] === "\xa2" && $datagram[$i+1] === 's' && $datagram[$i+2] === 'c') {
            $codes[] = ord($datagram[$i+3]) === 0xcc ? ord($datagram[$i+4]) : ord($datagram[$i+3]);
        }
    }
    echo 'root status: ' . (in_array(2, $codes, true) ? 'ERROR' : 'not error') . "\n";
    echo 'message present: ' . (strpos($datagram, 'ROOT_UNCAUGHT_227') !== false ? 'yes' : 'no') . "\n";
});

throw new RuntimeException('ROOT_UNCAUGHT_227');
?>
--EXPECTF--
%AFatal error: Uncaught RuntimeException: ROOT_UNCAUGHT_227%a
root status: ERROR
message present: yes
