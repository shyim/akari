--TEST--
CLI: setTransactionName() survives a later markAsWebTransaction()
--SKIPIF--
<?php
include __DIR__ . '/_skipif.inc';
if (!function_exists('socket_create')) die('skip ext-sockets not available');
// Probe the fixed exporter port; skip (rather than silently pass) if it's busy.
$probe = socket_create(AF_INET, SOCK_DGRAM, SOL_UDP);
if (!@socket_bind($probe, '127.0.0.1', 14320)) die('skip udp port 14320 busy');
socket_close($probe);
?>
--INI--
akari.enable=1
akari.trace_cli=1
akari.udp_host=127.0.0.1
akari.udp_port=14320
--FILE--
<?php
require __DIR__ . '/_msgpack_decode.inc';

$sock = socket_create(AF_INET, SOCK_DGRAM, SOL_UDP);
if (!socket_bind($sock, '127.0.0.1', 14320)) {
    throw new RuntimeException('failed to bind udp listener on 14320');
}

// Under trace_cli=1 the CLI root is active with an auto name like "php script".
// A custom name set before promotion must NOT be mistaken for the auto name and
// replaced with "CLI" — even when it happens to start with "php ". After
// markAsWebTransaction() the root becomes a SERVER transaction.
Akari\setTransactionName('php import');
Akari\markAsWebTransaction();
Akari\disable(); // finalize + export

socket_set_option($sock, SOL_SOCKET, SO_RCVTIMEO, ['sec' => 3, 'usec' => 0]);
$buf = '';
$n = socket_recvfrom($sock, $buf, 65535, 0, $from, $fromPort);
socket_close($sock);

$env = akari_msgpack_decode($buf);
$root = null;
foreach ($env['sp'] as $span) {
    if (isset($span['pv'])) { $root = $span; break; }
}

echo 'root name: ' . $root['n'] . "\n";
echo 'root kind (2=SERVER): ' . $root['k'] . "\n";
?>
--EXPECT--
root name: php import
root kind (2=SERVER): 2
