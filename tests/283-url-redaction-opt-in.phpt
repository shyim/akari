--TEST--
URL credentials and query strings are removed even with sensitive capture enabled
--SKIPIF--
<?php include __DIR__ . '/_skipif.inc'; ?>
--INI--
akari.enable=1
akari.capture_sensitive=1
--FILE--
<?php
@fopen('http://USERNAME_SENTINEL:URL_SENTINEL@127.0.0.1:1/path?token=QUERY_SENTINEL#FRAGMENT_SENTINEL', 'r');
@fopen('http://LONG_SENTINEL:' . str_repeat('x', 2048) . '@127.0.0.1:1/path', 'r');
$json = Akari\getSpansJson();
echo 'safe: ' . (!str_contains($json, '_SENTINEL') ? 'yes' : 'no') . "\n";
echo 'path retained: ' . (str_contains($json, '127.0.0.1:1/path') ? 'yes' : 'no') . "\n";
?>
--EXPECT--
safe: yes
path retained: yes
