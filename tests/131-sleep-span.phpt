--TEST--
Sleep: usleep creates a span with db.system=sleep
--SKIPIF--
<?php include __DIR__ . '/_skipif.inc'; ?>
--INI--
akari.enable=1
akari.trace_functions=0
--FILE--
<?php
usleep(1000); // 1ms

$json = Akari\getSpansJson();
$data = json_decode($json, true);
$spans = $data['resourceSpans'][0]['scopeSpans'][0]['spans'];

$found = false;
foreach ($spans as $span) {
    if ($span['name'] === 'usleep') {
        $found = true;
        echo "kind: " . ($span['kind'] == 1 ? 'INTERNAL' : $span['kind']) . "\n";
        foreach ($span['attributes'] as $attr) {
            if ($attr['key'] === 'db.system') {
                echo "db.system: " . $attr['value']['stringValue'] . "\n";
            }
        }
    }
}
echo "found: " . ($found ? 'yes' : 'no') . "\n";
?>
--EXPECT--
kind: INTERNAL
db.system: sleep
found: yes
