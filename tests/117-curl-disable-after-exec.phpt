--TEST--
Reusing a curl handle after Akari\disable() does not touch freed header memory
--SKIPIF--
<?php include __DIR__ . '/_curl_skipif.inc'; ?>
--INI--
akari.enable=1
--FILE--
<?php
require __DIR__ . '/_curl_server.inc';
$server = akari_curl_test_server_start();

// Profiled request with user headers: the hook installs a restored CURLOPT_
// HTTPHEADER slist on the handle that the extension owns.
$ch = curl_init($server['base_url'] . "/headers");
curl_setopt($ch, CURLOPT_RETURNTRANSFER, true);
curl_setopt($ch, CURLOPT_TIMEOUT, 10);
curl_setopt($ch, CURLOPT_HTTPHEADER, ["X-My-Header: first"]);
$result1 = curl_exec($ch);
if ($result1 === false) { echo "FAIL: request 1: " . curl_error($ch) . "\n"; exit; }

// Disable profiling mid-request. This used to free the restored slist that is
// still installed on the live $ch, leaving curl with a dangling pointer.
Akari\disable();

// Reuse the SAME handle with profiling off. curl dereferences the previously
// installed header list here — must not be freed memory (ASan would catch it).
$result2 = curl_exec($ch);
if ($result2 === false) { echo "FAIL: request 2: " . curl_error($ch) . "\n"; exit; }

$data2 = json_decode($result2, true);
$h = $data2["headers"];
echo "X-My-Header after disable+reuse: " . ($h["X-My-Header"] ?? "MISSING") . "\n";
echo "OK\n";
?>
--EXPECT--
X-My-Header after disable+reuse: first
OK
