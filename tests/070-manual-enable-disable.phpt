--TEST--
Manual enable/disable API works
--SKIPIF--
<?php include __DIR__ . '/_skipif.inc'; ?>
--INI--
akari.enable=0
--FILE--
<?php
// Verify the API functions work without crashing
// and that disable stops any active profiling
$r1 = Akari\enable();
$r2 = Akari\disable();

echo "enable returned: " . ($r1 === true ? "true" : "false") . "\n";
echo "disable returned: " . ($r2 === true ? "true" : "false") . "\n";

// Verify disable when nothing is active
$r3 = Akari\disable();
echo "disable again: " . ($r3 === true ? "true" : "false") . "\n";
echo "no crash\n";
?>
--EXPECT--
enable returned: true
disable returned: true
disable again: true
no crash
