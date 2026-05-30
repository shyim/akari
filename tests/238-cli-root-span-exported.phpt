--TEST--
CLI: the auto root span is exported over UDP with kind=INTERNAL
--SKIPIF--
<?php
include __DIR__ . '/_skipif.inc';
if (!function_exists('socket_create')) die('skip ext-sockets not available');
// Probe the fixed exporter port; skip (rather than silently pass) if it's busy.
$probe = socket_create(AF_INET, SOCK_DGRAM, SOL_UDP);
if (!@socket_bind($probe, '127.0.0.1', 14319)) die('skip udp port 14319 busy');
socket_close($probe);
?>
--INI--
akari.enable=1
akari.trace_cli=1
akari.udp_host=127.0.0.1
akari.udp_port=14319
--ARGS--
asset:install --no-debug
--FILE--
<?php
require __DIR__ . '/_msgpack_decode.inc';

// The exporter destination is fixed at MINIT from the INI above, so bind a
// listener to that same port to capture the real on-the-wire payload — not
// just live introspection.
$sock = socket_create(AF_INET, SOCK_DGRAM, SOL_UDP);
if (!socket_bind($sock, '127.0.0.1', 14319)) {
    throw new RuntimeException('failed to bind udp listener on 14319');
}

// disable() finalizes the CLI root span and exports it via the UDP exporter.
Akari\disable();

socket_set_option($sock, SOL_SOCKET, SO_RCVTIMEO, ['sec' => 3, 'usec' => 0]);
$buf = '';
$n = socket_recvfrom($sock, $buf, 65535, 0, $from, $fromPort);
socket_close($sock);

echo 'received: ' . ($n > 0 ? 'yes' : 'no') . "\n";

$env = akari_msgpack_decode($buf);
// Envelope: {v, sn, ti, sp: [spans]}. The root span carries the "pv" (php
// version) field that only init_root_runtime_annotations() writes.
$root = null;
foreach ($env['sp'] as $span) {
    if (isset($span['pv'])) { $root = $span; break; }
}

echo 'root present: ' . ($root !== null ? 'yes' : 'no') . "\n";
// Span name is "php <script-basename> <args>"; the CLI args must be inlined.
echo 'name starts php: ' . (str_starts_with($root['n'], 'php ') ? 'yes' : 'no') . "\n";
echo 'name has args: ' . (str_ends_with($root['n'], ' asset:install --no-debug') ? 'yes' : 'no') . "\n";
echo 'root kind (1=INTERNAL): ' . $root['k'] . "\n";
echo 'root sapi: ' . $root['ps'] . "\n";
// process.command_line is the full command: PHP executable + full argv (script
// path, not just basename) + args.
echo 'command_line has php binary: ' . (str_contains($root['pc'], PHP_BINARY) ? 'yes' : 'no') . "\n";
echo 'command_line has full script path: ' . (str_contains($root['pc'], __FILE__) ? 'yes' : 'no') . "\n";
echo 'command_line has args: ' . (str_ends_with($root['pc'], ' asset:install --no-debug') ? 'yes' : 'no') . "\n";
?>
--EXPECT--
received: yes
root present: yes
name starts php: yes
name has args: yes
root kind (1=INTERNAL): 1
root sapi: cli
command_line has php binary: yes
command_line has full script path: yes
command_line has args: yes
