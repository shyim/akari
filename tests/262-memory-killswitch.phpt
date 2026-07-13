--TEST--
akari.disable_at_memory_percentage skips profiling when memory usage is over the limit
--SKIPIF--
<?php
include __DIR__ . '/_skipif.inc';
if (memory_get_peak_usage(true) === 0) {
    // Zend MM disabled (USE_ZEND_ALLOC=0, e.g. the ASan build): PHP does not
    // track usage on the std-malloc heap, so the kill-switch can never trip.
    die('skip peak memory usage not tracked (Zend MM disabled)');
}
?>
--INI--
akari.enable=1
akari.trace_cli=1
memory_limit=2M
akari.disable_at_memory_percentage=1
--FILE--
<?php
// With a 2M limit and a 1% disable threshold (~20KB), the interpreter's own
// baseline already exceeds the threshold at request start, so profiling is
// never activated for this request and no spans are recorded.
var_dump(Akari\isProfiling());
echo 'span count: ' . Akari\getSpanCount() . "\n";
echo 'spans json: ' . var_export(Akari\getSpansJson(), true) . "\n";
?>
--EXPECT--
bool(false)
span count: 0
spans json: false
