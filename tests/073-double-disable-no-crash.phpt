--TEST--
Double disable is safe and clears span count
--SKIPIF--
<?php include __DIR__ . '/_skipif.inc'; ?>
--INI--
akari.enable=0
--FILE--
<?php
// Enable manually, create some spans
Akari\enable();

function work() { usleep(1); }
work();

$count_before = Akari\getSpanCount();

// First disable: exports spans, cleans up
Akari\disable();
$count_after_first = Akari\getSpanCount();

// Second disable: should be safe (no crash, no double-export)
Akari\disable();
$count_after_second = Akari\getSpanCount();

echo "spans before disable: " . $count_before . "\n";
echo "spans after first disable: " . $count_after_first . "\n";
echo "spans after second disable: " . $count_after_second . "\n";
echo "no crash\n";
?>
--EXPECTF--
spans before disable: %d
spans after first disable: 0
spans after second disable: 0
no crash
