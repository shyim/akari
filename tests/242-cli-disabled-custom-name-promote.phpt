--TEST--
CLI: with trace_cli=0, setTransactionName() before markAsWebTransaction() is kept and runtime env populated
--SKIPIF--
<?php
include __DIR__ . '/_skipif.inc';
if (!function_exists('socket_create')) die('skip ext-sockets not available');
$probe = socket_create(AF_INET, SOCK_DGRAM, SOL_UDP);
if (!@socket_bind($probe, '127.0.0.1', 14323)) die('skip udp port 14323 busy');
socket_close($probe);
?>
--INI--
akari.enable=1
akari.trace_cli=0
akari.udp_host=127.0.0.1
akari.udp_port=14323
--FILE--
<?php
require __DIR__ . '/_msgpack_decode.inc';

// With trace_cli=0 the CLI root is inactive when setTransactionName() runs, so
// the name is only stashed in custom_transaction_name. markAsWebTransaction()
// must still (a) apply that name (not fall back to "CLI") and (b) populate the
// runtime annotations that the inactive path skipped (php.version / php.sapi).
$sock = socket_create(AF_INET, SOCK_DGRAM, SOL_UDP);
if (!socket_bind($sock, '127.0.0.1', 14323)) {
    throw new RuntimeException('failed to bind udp listener on 14323');
}

Akari\setTransactionName('my-import-job');
Akari\markAsWebTransaction();
Akari\disable(); // finalize + export

socket_set_option($sock, SOL_SOCKET, SO_RCVTIMEO, ['sec' => 3, 'usec' => 0]);
$buf = '';
socket_recvfrom($sock, $buf, 65535, 0, $from, $fromPort);
socket_close($sock);

$env = akari_msgpack_decode($buf);
$root = null;
foreach ($env['sp'] as $span) {
    if (isset($span['ps'])) { $root = $span; break; }
}

echo 'root name: ' . $root['n'] . "\n";
echo 'root kind (2=SERVER): ' . $root['k'] . "\n";
echo 'php.version present: ' . (!empty($root['pv']) ? 'yes' : 'no') . "\n";
echo 'php.sapi: ' . ($root['ps'] ?? '<empty>') . "\n";
?>
--EXPECT--
root name: my-import-job
root kind (2=SERVER): 2
php.version present: yes
php.sapi: cli
