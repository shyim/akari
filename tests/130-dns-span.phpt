--TEST--
DNS: gethostbyname creates a span with db.system=dns
--SKIPIF--
<?php include __DIR__ . '/_skipif.inc'; ?>
--INI--
akari.enable=1
akari.trace_functions=0
--FILE--
<?php
gethostbyname("localhost");

$json = Akari\getSpansJson();
$data = json_decode($json, true);
$spans = $data['resourceSpans'][0]['scopeSpans'][0]['spans'];

$found = false;
foreach ($spans as $span) {
    if ($span['name'] === 'gethostbyname') {
        $found = true;
        // Check span kind = CLIENT (3)
        echo "kind: " . ($span['kind'] == 3 ? 'CLIENT' : $span['kind']) . "\n";
        // Check db.system attribute
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
echo "found: " . ($found ? 'yes' : 'no') . "\n";
?>
--EXPECT--
kind: CLIENT
db.system: dns
db.statement: localhost
found: yes
