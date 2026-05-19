--TEST--
Shell: exec creates a span with db.system=shell and command in db.statement
--SKIPIF--
<?php include __DIR__ . '/_skipif.inc'; ?>
--INI--
akari.enable=1
akari.trace_functions=0
--FILE--
<?php
exec("echo hello");

$json = Akari\getSpansJson();
$data = json_decode($json, true);
$spans = $data['resourceSpans'][0]['scopeSpans'][0]['spans'];

foreach ($spans as $span) {
    if ($span['name'] === 'exec') {
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
db.system: shell
db.statement: echo hello
