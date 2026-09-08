--TEST--
UDP export authenticates its complete payload with HMAC-SHA256 and a random nonce
--SKIPIF--
<?php include __DIR__ . '/_skipif.inc'; ?>
--ENV--
AKARI_UDP_KEY=0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef
--INI--
akari.enable=1
akari.trace_cli=1
akari.udp_port=15379
--FILE--
<?php
require __DIR__ . '/_msgpack_decode.inc';
$socket = stream_socket_server('udp://127.0.0.1:15379', $errno, $errstr, STREAM_SERVER_BIND);
if (!$socket) throw new RuntimeException($errstr);
stream_set_timeout($socket, 2);
Akari\createSpan('authenticated');
Akari\disable();
$packet = stream_socket_recvfrom($socket, 65535);
fclose($socket);
echo 'version: ' . substr($packet, 0, 4) . "\n";
$mac = hash_hmac('sha256', substr($packet, 0, 28) . substr($packet, 60), getenv('AKARI_UDP_KEY'), true);
echo 'MAC valid: ' . (hash_equals($mac, substr($packet, 28, 32)) ? 'yes' : 'no') . "\n";
$seconds = unpack('J', substr($packet, 4, 8))[1];
echo 'timestamp current: ' . (abs(time() - $seconds) <= 30 ? 'yes' : 'no') . "\n";
echo 'nonce present: ' . (substr($packet, 12, 16) !== str_repeat("\0", 16) ? 'yes' : 'no') . "\n";
echo 'payload valid: ' . (akari_msgpack_decode(substr($packet, 60))['v'] === 1 ? 'yes' : 'no') . "\n";
?>
--EXPECT--
version: AKR1
MAC valid: yes
timestamp current: yes
nonce present: yes
payload valid: yes
