--TEST--
PCRE: fast preg_match does NOT create a span (threshold filtering)
--SKIPIF--
<?php include __DIR__ . '/_skipif.inc'; ?>
--INI--
akari.enable=1
akari.trace_functions=0
--FILE--
<?php
// A simple fast regex should be under the 1ms threshold — no span
preg_match('/^hello$/', 'hello');
preg_replace('/\d/', 'X', 'abc');

$count = Akari\getSpanCount();
echo "spans for fast pcre: $count\n";
?>
--EXPECT--
spans for fast pcre: 0
