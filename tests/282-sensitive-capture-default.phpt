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
stream_set_timeout($socket, 1);
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
while (($packet = @stream_socket_recvfrom($socket, 65535)) !== false && $packet !== '') $wire .= $packet;
fclose($socket);
echo 'wire received: ' . ($wire !== '' ? 'yes' : 'no') . "\n";
echo 'wire safe: ' . (!str_contains($wire, '_SENTINEL') ? 'yes' : 'no') . "\n";
echo 'labels retained: ' . (str_contains($wire, 'safe-label') ? 'yes' : 'no') . "\n";
?>
--EXPECT--
debug safe: yes
wire received: yes
wire safe: yes
labels retained: yes
