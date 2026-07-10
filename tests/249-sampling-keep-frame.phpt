--TEST--
Non-sampled (sample_rate=0) request exports root + layer summary but no child spans
--SKIPIF--
<?php
include __DIR__ . '/_skipif.inc';
if (!function_exists('socket_create')) die('skip ext-sockets not available');
if (!class_exists('PDO') || !in_array('sqlite', PDO::getAvailableDrivers(), true)) die('skip pdo_sqlite not available');
$probe = socket_create(AF_INET, SOCK_DGRAM, SOL_UDP);
if (!@socket_bind($probe, '127.0.0.1', 14321)) die('skip udp port 14321 busy');
socket_close($probe);
?>
--INI--
akari.enable=1
akari.trace_cli=1
akari.udp_host=127.0.0.1
akari.udp_port=14321
akari.sample_rate=0
--FILE--
<?php
require __DIR__ . '/_msgpack_decode.inc';

$sock = socket_create(AF_INET, SOCK_DGRAM, SOL_UDP);
socket_bind($sock, '127.0.0.1', 14321);

// Same DB work as the sampled test — but at sample_rate=0 none of these become
// child spans; only the root + layer summary should ship ("keep-frame").
$pdo = new PDO('sqlite::memory:');
$pdo->exec('CREATE TABLE t (id INTEGER PRIMARY KEY)');
for ($i = 0; $i < 5; $i++) $pdo->exec("INSERT INTO t DEFAULT VALUES");

Akari\disable();

socket_set_option($sock, SOL_SOCKET, SO_RCVTIMEO, ['sec' => 3, 'usec' => 0]);
$buf = '';
$n = socket_recvfrom($sock, $buf, 65535, 0, $from, $fromPort);
socket_close($sock);

$env = akari_msgpack_decode($buf);
$spans = $env['sp'];

// Split root (carries "pv") from child spans.
$root = null; $children = 0;
foreach ($spans as $span) {
    if (isset($span['pv'])) { $root = $span; } else { $children++; }
}

echo 'root present: ' . ($root !== null ? 'yes' : 'no') . "\n";
echo 'child span count: ' . $children . "\n";
echo 'sampled flag: ' . $root['sd'] . "\n";
// The layer breakdown must survive even though the child-span tree does not.
echo 'db count (kept): ' . $root['ly']['db'][1] . "\n";
?>
--EXPECT--
root present: yes
child span count: 0
sampled flag: 0
db count (kept): 6
