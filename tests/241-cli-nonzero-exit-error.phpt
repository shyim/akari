--TEST--
CLI: a non-zero exit code marks the root span as error (even for caught exceptions)
--SKIPIF--
<?php
include __DIR__ . '/_skipif.inc';
if (!function_exists('socket_create')) die('skip ext-sockets not available');
$probe = socket_create(AF_INET, SOCK_DGRAM, SOL_UDP);
if (!@socket_bind($probe, '127.0.0.1', 14322)) die('skip udp port 14322 busy');
socket_close($probe);
?>
--INI--
akari.enable=1
akari.trace_cli=1
akari.udp_host=127.0.0.1
akari.udp_port=14322
--FILE--
<?php
require __DIR__ . '/_msgpack_decode.inc';

// A command that catches its exception, logs it, and exits non-zero (the common
// Symfony console / Laravel artisan pattern) must surface as a failed CLI span:
// the underlying exception never escapes, so only the exit code reveals failure.
$sock = socket_create(AF_INET, SOCK_DGRAM, SOL_UDP);
if (!socket_bind($sock, '127.0.0.1', 14322)) {
    throw new RuntimeException('failed to bind udp listener on 14322');
}

register_shutdown_function(function () use ($sock) {
    Akari\disable(); // finalize + export after the exit() unwind

    socket_set_option($sock, SOL_SOCKET, SO_RCVTIMEO, ['sec' => 3, 'usec' => 0]);
    $buf = '';
    socket_recvfrom($sock, $buf, 65535, 0, $from, $fromPort);
    socket_close($sock);

    $env = akari_msgpack_decode($buf);
    $root = null;
    foreach ($env['sp'] as $span) {
        if (isset($span['pv'])) { $root = $span; break; }
    }
    echo 'root status (2=ERROR): ' . $root['sc'] . "\n";
    echo 'message: ' . $root['sm'] . "\n";
});

try {
    throw new RuntimeException('Command "bla" is not defined.');
} catch (Throwable $e) {
    fwrite(STDERR, $e->getMessage() . "\n");
    exit(1);
}
?>
--EXPECTF--
Command "bla" is not defined.
root status (2=ERROR): 2
message: exited with code 1
