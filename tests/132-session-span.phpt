--TEST--
Session: session_start creates span, session_write_close only if slow
--SKIPIF--
<?php include __DIR__ . '/_skipif.inc'; ?>
--INI--
akari.enable=1
akari.trace_functions=0
session.save_handler=files
session.save_path=/tmp
--FILE--
<?php
session_start();
session_write_close();

$json = Akari\getSpansJson();
$data = json_decode($json, true);
$spans = $data['resourceSpans'][0]['scopeSpans'][0]['spans'];
$names = array_column($spans, 'name');

echo "has session_start: " . (in_array('session_start', $names) ? 'yes' : 'no') . "\n";
// session_write_close has 1ms threshold — fast local file session should be skipped
echo "has session_write_close: " . (in_array('session_write_close', $names) ? 'yes' : 'no') . "\n";

// Check db.system for session_start
foreach ($spans as $span) {
    if ($span['name'] === 'session_start') {
        foreach ($span['attributes'] as $attr) {
            if ($attr['key'] === 'db.system') {
                echo "db.system: " . $attr['value']['stringValue'] . "\n";
            }
        }
    }
}
?>
--EXPECT--
has session_start: yes
has session_write_close: no
db.system: session
