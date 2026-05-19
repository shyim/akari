--TEST--
Span storage grows beyond initial capacity (2000 calls)
--SKIPIF--
<?php include __DIR__ . '/_skipif.inc'; ?>
--INI--
akari.enable=1
akari.trace_functions=1
--FILE--
<?php
function work() {
    usleep(1);
}

for ($i = 0; $i < 2000; $i++) {
    work();
}

$count = Akari\getSpanCount();
// At least 2000 (work calls) — may include file-level span too
echo "grew past 1024: " . ($count >= 2000 ? 'yes' : 'no') . "\n";
?>
--EXPECT--
grew past 1024: yes
