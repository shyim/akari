--TEST--
CLI: a script ending in exit()/die() is NOT marked as an error span
--SKIPIF--
<?php
include __DIR__ . '/_skipif.inc';
if (!function_exists('socket_create')) die('skip ext-sockets not available');
$probe = socket_create(AF_INET, SOCK_DGRAM, SOL_UDP);
if (!@socket_bind($probe, '127.0.0.1', 14321)) die('skip udp port 14321 busy');
socket_close($probe);
?>
--INI--
akari.enable=1
akari.trace_cli=1
akari.udp_host=127.0.0.1
akari.udp_port=14321
--FILE--
<?php
require __DIR__ . '/_msgpack_decode.inc';

// exit()/die() unwinds through the engine's exception machinery, but it is not
// an error — the CLI root span must stay UNSET (status 0), not ERROR (status 2).
// Capture the export from a shutdown function, which runs after exit() unwinds.
$sock = socket_create(AF_INET, SOCK_DGRAM, SOL_UDP);
if (!socket_bind($sock, '127.0.0.1', 14321)) {
    throw new RuntimeException('failed to bind udp listener on 14321');
}

register_shutdown_function(function () use ($sock) {
    Akari\disable(); // finalize + export the root after the exit() unwind

    socket_set_option($sock, SOL_SOCKET, SO_RCVTIMEO, ['sec' => 3, 'usec' => 0]);
    $buf = '';
    socket_recvfrom($sock, $buf, 65535, 0, $from, $fromPort);
    socket_close($sock);

    $env = akari_msgpack_decode($buf);
    $root = null;
    foreach ($env['sp'] as $span) {
        if (isset($span['pv'])) { $root = $span; break; }
    }
    echo 'root present: ' . ($root !== null ? 'yes' : 'no') . "\n";
    echo 'root status (0=UNSET): ' . $root['sc'] . "\n";
});

echo "working\n";
exit(0);
?>
--EXPECT--
working
root present: yes
root status (0=UNSET): 0
