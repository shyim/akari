--TEST--
cURL tracks accepted header changes and resets while profiling is disabled
--SKIPIF--
<?php include __DIR__ . '/_curl_skipif.inc'; ?>
--INI--
akari.enable=0
--FILE--
<?php
require __DIR__ . '/_curl_server.inc';
$server = akari_curl_test_server_start();
$url = $server['base_url'] . '/headers';
$ch = curl_init($url);
curl_setopt_array($ch, [CURLOPT_RETURNTRANSFER => true, CURLOPT_HTTPHEADER => ['X-Token: before']]);
Akari\enable();
echo 'before enable: ', json_decode(curl_exec($ch), true)['headers']['X-Token'] ?? 'missing', "\n";
Akari\disable();
curl_setopt($ch, CURLOPT_HTTPHEADER, ['X-Token: changed']);
Akari\enable();
echo 'changed while disabled: ', json_decode(curl_exec($ch), true)['headers']['X-Token'] ?? 'missing', "\n";
Akari\disable();
curl_reset($ch);
curl_setopt_array($ch, [CURLOPT_URL => $url, CURLOPT_RETURNTRANSFER => true]);
Akari\enable();
$headers = json_decode(curl_exec($ch), true)['headers'];
echo 'reset while disabled: ', !isset($headers['X-Token']) ? 'clean' : 'stale', "\n";
Akari\disable();
?>
--EXPECT--
before enable: before
changed while disabled: changed
reset while disabled: clean
