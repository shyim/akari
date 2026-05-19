--TEST--
Mail: mail() creates a span with db.system=mail and recipient in db.statement
--SKIPIF--
<?php
include __DIR__ . '/_skipif.inc';
if (PHP_OS_FAMILY === 'Windows') die('skip not on Windows');
?>
--INI--
akari.enable=1
akari.trace_functions=0
sendmail_path=/usr/bin/true
--FILE--
<?php
@mail("test@example.com", "subject", "body");

$json = Akari\getSpansJson();
$data = json_decode($json, true);
$spans = $data['resourceSpans'][0]['scopeSpans'][0]['spans'];

foreach ($spans as $span) {
    if ($span['name'] === 'mail') {
        echo "found: yes\n";
        foreach ($span['attributes'] as $attr) {
            if ($attr['key'] === 'db.system') {
                echo "db.system: " . $attr['value']['stringValue'] . "\n";
            }
            if ($attr['key'] === 'db.statement') {
                echo "db.statement: " . $attr['value']['stringValue'] . "\n";
            }
        }
    }
}
?>
--EXPECT--
found: yes
db.system: mail
db.statement: test@example.com
