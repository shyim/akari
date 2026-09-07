--TEST--
cURL authorization headers do not survive reset or leak to a new handle
--SKIPIF--
<?php include __DIR__ . '/_curl_skipif.inc'; ?>
--INI--
akari.enable=1
--FILE--
<?php
require __DIR__ . '/_curl_server.inc';
$server = akari_curl_test_server_start();
$url = $server['base_url'] . '/headers';
$ch = curl_init($url);
curl_setopt_array($ch, [CURLOPT_RETURNTRANSFER => true, CURLOPT_HTTPHEADER => ['Authorization: Bearer private']]);
$headers = json_decode(curl_exec($ch), true)['headers'];
echo 'initial auth: ', $headers['Authorization'], "\n";
curl_reset($ch);
curl_setopt_array($ch, [CURLOPT_URL => $url, CURLOPT_RETURNTRANSFER => true]);
$headers = json_decode(curl_exec($ch), true)['headers'];
echo 'reset auth absent: ', !isset($headers['Authorization']) ? 'yes' : 'no', "\n";

// Seed the cache again, then destroy the handle without resetting it.
curl_setopt($ch, CURLOPT_HTTPHEADER, ['Authorization: Bearer private']);
$weak = WeakReference::create($ch);
unset($ch);
echo 'handle released: ', $weak->get() === null ? 'yes' : 'no', "\n";
$leaked = false;
for ($i = 0; $i < 8; $i++) {
    $ch = curl_init($url);
    curl_setopt($ch, CURLOPT_RETURNTRANSFER, true);
    $headers = json_decode(curl_exec($ch), true)['headers'];
    $leaked |= isset($headers['Authorization']);
    unset($ch);
}
echo 'new handles auth absent: ', !$leaked ? 'yes' : 'no', "\n";
?>
--EXPECT--
initial auth: Bearer private
reset auth absent: yes
handle released: yes
new handles auth absent: yes
