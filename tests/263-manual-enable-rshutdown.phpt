--TEST--
Manual enable is finalized and exported during request shutdown
--SKIPIF--
<?php
include __DIR__ . '/_skipif.inc';
if (!function_exists('proc_open')) die('skip proc_open not available');
$sock = @stream_socket_server('udp://127.0.0.1:54339', $errno, $errstr, STREAM_SERVER_BIND);
if (!$sock) die('skip udp port unavailable');
fclose($sock);
?>
--INI--
akari.enable=0
--FILE--
<?php
$sock = stream_socket_server('udp://127.0.0.1:54339', $errno, $errstr, STREAM_SERVER_BIND);
if (!$sock) {
    throw new RuntimeException('failed to bind UDP listener');
}

$extension = dirname(__DIR__) . '/modules/akari.so';
$process = proc_open([
    PHP_BINARY,
    '-n',
    '-d', 'extension=' . $extension,
    '-d', 'akari.enable=0',
    '-d', 'akari.trace_functions=0',
    '-d', 'akari.udp_host=127.0.0.1',
    '-d', 'akari.udp_port=54339',
    '-r', 'Akari\\enable(); Akari\\createSpan("manual-rshutdown-span");',
], [
    1 => ['pipe', 'w'],
    2 => ['pipe', 'w'],
], $pipes);

$stdout = stream_get_contents($pipes[1]);
$stderr = stream_get_contents($pipes[2]);
fclose($pipes[1]);
fclose($pipes[2]);
$exitCode = proc_close($process);

stream_set_blocking($sock, false);
$read = [$sock];
$write = null;
$except = null;
$ready = stream_select($read, $write, $except, 2, 0);
$datagram = $ready ? stream_socket_recvfrom($sock, 8192) : '';
fclose($sock);

echo 'child exited cleanly: ' . ($exitCode === 0 ? 'yes' : 'no') . "\n";
echo 'received datagram: ' . ($datagram !== '' ? 'yes' : 'no') . "\n";
echo 'manual span exported: ' . (str_contains($datagram, 'manual-rshutdown-span') ? 'yes' : 'no') . "\n";
?>
--EXPECT--
child exited cleanly: yes
received datagram: yes
manual span exported: yes
