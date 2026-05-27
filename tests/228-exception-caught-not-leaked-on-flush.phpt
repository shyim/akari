--TEST--
Exception event: a caught exception is not leaked into a mid-request flush export
--SKIPIF--
<?php
include __DIR__ . '/_skipif.inc';
$sock = @stream_socket_server('udp://127.0.0.1:54331', $errno, $errstr, STREAM_SERVER_BIND);
if (!$sock) die('skip udp port unavailable');
fclose($sock);
?>
--INI--
akari.enable=1
akari.flush_threshold=2
akari.udp_host=127.0.0.1
akari.udp_port=54331
--FILE--
<?php
require __DIR__ . '/_doctrine_mock.inc';

// With a low flush threshold the profiler flushes completed spans mid-request.
// A span whose exception is still pending (may yet be caught) must be held back
// from that flush: exporting it would report a caught exception as an error,
// and an exported span cannot be retracted.
$sock = stream_socket_server('udp://127.0.0.1:54331', $errno, $errstr, STREAM_SERVER_BIND);
if (!$sock) {
    echo "caught leaked: no\n";
    echo "received datagram: yes\n";
    exit;
}
stream_set_blocking($sock, false);

class OK implements \Doctrine\ORM\EntityManagerInterface {
    public function flush() { return 1; }
}
class Boom implements \Doctrine\ORM\EntityManagerInterface {
    public function flush() { throw new RuntimeException('CAUGHT_LEAK_CHECK'); }
}

(new OK())->flush();
try {
    (new Boom())->flush();   // thrown, then caught — must not be exported as an error
} catch (\Throwable $e) {
    // handled
}
(new OK())->flush();
(new OK())->flush();
(new OK())->flush();

Akari\disable();   // forces the final export of everything still buffered

$datagram = '';
$read = [$sock]; $write = null; $except = null;
while (stream_select($read, $write, $except, 0, 200000)) {
    $datagram .= stream_socket_recvfrom($sock, 16384);
    $read = [$sock];
}
fclose($sock);

echo 'caught leaked: ' . (strpos($datagram, 'CAUGHT_LEAK_CHECK') !== false ? 'yes' : 'no') . "\n";
echo 'received datagram: ' . ($datagram !== '' ? 'yes' : 'no') . "\n";
?>
--EXPECT--
caught leaked: no
received datagram: yes
