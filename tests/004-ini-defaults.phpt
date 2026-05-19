--TEST--
Verify INI setting defaults
--SKIPIF--
<?php include __DIR__ . '/_skipif.inc'; ?>
--FILE--
<?php
echo 'enable: ' . ini_get('akari.enable') . "\n";
echo 'service_name: ' . ini_get('akari.service_name') . "\n";
echo 'max_depth: ' . ini_get('akari.max_depth') . "\n";
echo 'trace_internal: ' . ini_get('akari.trace_internal') . "\n";
echo 'mode: ' . ini_get('akari.mode') . "\n";
echo 'udp_host: ' . ini_get('akari.udp_host') . "\n";
echo 'udp_port: ' . ini_get('akari.udp_port') . "\n";
?>
--EXPECT--
enable: 0
service_name: php
max_depth: 64
trace_internal: 0
mode: trace
udp_host: 127.0.0.1
udp_port: 4319
