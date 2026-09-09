--TEST--
Sensitive text is removed from debug and wire telemetry by default
--SKIPIF--
<?php
include __DIR__ . '/_skipif.inc';
if (!class_exists('PDO') || !in_array('sqlite', PDO::getAvailableDrivers())) die('skip requires PDO SQLite');
?>
--INI--
akari.enable=1
akari.trace_cli=1
akari.capture_sensitive=0
akari.udp_port=15380
--ARGS--
--password=CLI_SENTINEL
--FILE--
<?php
$socket = stream_socket_server('udp://127.0.0.1:15380', $errno, $errstr, STREAM_SERVER_BIND);
if (!$socket) throw new RuntimeException($errstr);
require __DIR__ . '/_msgpack_decode.inc';
$db = new PDO('sqlite::memory:');
$db->query("SELECT 'SQL_SENTINEL'");
@fopen('http://USERNAME_SENTINEL:URL_SENTINEL@127.0.0.1:1/path?token=QUERY_SENTINEL#FRAGMENT_SENTINEL', 'r');
Akari\createSpan('safe-label');
Akari\addTag('credential', 'TAG_SENTINEL');
Akari\logException(new RuntimeException('EXCEPTION_SENTINEL'));
Akari\log('error', 'LOG_SENTINEL', ['password' => 'CONTEXT_SENTINEL']);
$debug = Akari\getSpansJson() . Akari\getLogsJson();
echo 'debug safe: ' . (!str_contains($debug, '_SENTINEL') ? 'yes' : 'no') . "\n";
Akari\disable();
stream_set_blocking($socket, false);
$wire = '';
$signals = [];
$deadline = microtime(true) + 2;
while (count($signals) < 2 && ($remaining = $deadline - microtime(true)) > 0) {
    // UDP delivery can lag sendto() on macOS. Wait for readiness rather than
    // treating a momentarily empty nonblocking socket as end of transmission.
    $read = [$socket];
    $write = $except = [];
    if (stream_select($read, $write, $except, 0, (int) min($remaining * 1_000_000, 999_999)) === false) {
        throw new RuntimeException('UDP readiness check failed');
    }
    if (!$read) continue;
    $packet = stream_socket_recvfrom($socket, 65535);
    if ($packet === false || $packet === '') continue;
    $wire .= $packet;
    $datagram = akari_msgpack_decode($packet);
    if (isset($datagram['sp'])) $signals['spans'] = true;
    if (isset($datagram['lg'])) $signals['logs'] = true;
}
fclose($socket);
echo 'wire received: ' . ($wire !== '' ? 'yes' : 'no') . "\n";
echo 'both signals received: ' . (count($signals) === 2 ? 'yes' : 'no') . "\n";
echo 'wire safe: ' . (!str_contains($wire, '_SENTINEL') ? 'yes' : 'no') . "\n";
echo 'labels retained: ' . (str_contains($wire, 'safe-label') ? 'yes' : 'no') . "\n";
?>
--EXPECT--
debug safe: yes
wire received: yes
both signals received: yes
wire safe: yes
labels retained: yes
