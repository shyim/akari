--TEST--
Function called 100 times in loop: 1 frame, >= 100 spans
--SKIPIF--
<?php include __DIR__ . '/_skipif.inc'; ?>
--INI--
akari.enable=1
akari.trace_functions=1
--FILE--
<?php
function tick() {
    return 1;
}

for ($i = 0; $i < 100; $i++) {
    tick();
}

$frames = Akari\getFrameCount();
$spans = Akari\getSpanCount();

// tick is a single unique frame
echo "frames_ok: " . (($frames >= 1) ? "yes" : "no, got $frames") . "\n";
// 100 calls produce at least 100 spans
echo "spans_ok: " . (($spans >= 100) ? "yes" : "no, got $spans") . "\n";
?>
--EXPECT--
frames_ok: yes
spans_ok: yes
