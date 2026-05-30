--TEST--
Verify INI setting defaults
--SKIPIF--
<?php include __DIR__ . '/_skipif.inc'; ?>
--FILE--
<?php
echo 'enable: ' . ini_get('akari.enable') . "\n";
echo 'service_name: [' . ini_get('akari.service_name') . "]\n";
echo 'max_depth: ' . ini_get('akari.max_depth') . "\n";
echo 'min_duration_ms: ' . ini_get('akari.min_duration_ms') . "\n";
echo 'udp_host: ' . ini_get('akari.udp_host') . "\n";
echo 'udp_port: ' . ini_get('akari.udp_port') . "\n";
echo 'trace_cli: ' . ini_get('akari.trace_cli') . "\n";
?>
--EXPECT--
enable: 0
service_name: []
max_depth: 64
min_duration_ms: 0
udp_host: 127.0.0.1
udp_port: 4319
trace_cli: 1
