--TEST--
Root span carries a per-layer time summary (app + db) exported over UDP
--SKIPIF--
<?php
include __DIR__ . '/_skipif.inc';
if (!function_exists('socket_create')) die('skip ext-sockets not available');
if (!class_exists('PDO') || !in_array('sqlite', PDO::getAvailableDrivers(), true)) die('skip pdo_sqlite not available');
$probe = socket_create(AF_INET, SOCK_DGRAM, SOL_UDP);
if (!@socket_bind($probe, '127.0.0.1', 14320)) die('skip udp port 14320 busy');
socket_close($probe);
?>
--INI--
akari.enable=1
akari.trace_cli=1
akari.udp_host=127.0.0.1
akari.udp_port=14320
akari.sample_rate=1.0
--FILE--
<?php
require __DIR__ . '/_msgpack_decode.inc';

$sock = socket_create(AF_INET, SOCK_DGRAM, SOL_UDP);
socket_bind($sock, '127.0.0.1', 14320);

// Generate DB-layer time (pdo_sqlite) and some app time.
$pdo = new PDO('sqlite::memory:');
$pdo->exec('CREATE TABLE t (id INTEGER PRIMARY KEY)');
for ($i = 0; $i < 5; $i++) $pdo->exec("INSERT INTO t DEFAULT VALUES");

Akari\disable();

socket_set_option($sock, SOL_SOCKET, SO_RCVTIMEO, ['sec' => 3, 'usec' => 0]);
$buf = '';
$n = socket_recvfrom($sock, $buf, 65535, 0, $from, $fromPort);
socket_close($sock);

$env = akari_msgpack_decode($buf);
$root = null;
foreach ($env['sp'] as $span) {
    if (isset($span['pv'])) { $root = $span; break; }
}

echo 'root present: ' . ($root !== null ? 'yes' : 'no') . "\n";
echo 'has layer summary: ' . (isset($root['ly']) ? 'yes' : 'no') . "\n";
echo 'has app layer: ' . (isset($root['ly']['app']) ? 'yes' : 'no') . "\n";
echo 'has db layer: ' . (isset($root['ly']['db']) ? 'yes' : 'no') . "\n";
// db layer is [duration_ns, count]; 6 exec() calls (CREATE + 5 INSERT).
echo 'db count: ' . $root['ly']['db'][1] . "\n";
echo 'db duration > 0: ' . ($root['ly']['db'][0] > 0 ? 'yes' : 'no') . "\n";
echo 'sampled flag: ' . $root['sd'] . "\n";
?>
--EXPECT--
root present: yes
has layer summary: yes
has app layer: yes
has db layer: yes
db count: 6
db duration > 0: yes
sampled flag: 1
