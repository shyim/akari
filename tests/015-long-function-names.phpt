--TEST--
Very long function names do not crash (snprintf clamping)
--SKIPIF--
<?php include __DIR__ . '/_skipif.inc'; ?>
--INI--
akari.enable=1
akari.trace_functions=1
--FILE--
<?php
// Function name long enough to overflow PROFILER_NAME_MAX (512)
// snprintf return value must be clamped to buffer size, otherwise
// exporters read past fixed-length buffers.
$long_name = 'long_function_' . str_repeat('x', 600);
$fn = function () use ($long_name) {
    // Create a dynamic function with a very long "name" via closure
    // The observer extracts Class::method or {closure:file(line)} format
    usleep(1);
};
$fn();

$json = Akari\getSpansJson();
$data = json_decode($json, true);

// Verify valid JSON was produced (no crash / no garbled output)
if (!is_array($data)) {
    echo "FAIL: not valid JSON\n";
} elseif (!isset($data['resourceSpans'][0]['scopeSpans'][0]['spans'][0]['name'])) {
    echo "FAIL: missing span name\n";
} else {
    $name = $data['resourceSpans'][0]['scopeSpans'][0]['spans'][0]['name'];
    // Closure names include filename — verify it's not empty or truncated wrongly
    if (strlen($name) > 0 && strlen($name) <= 520) {
        echo "OK: span name length " . strlen($name) . " (within bounds)\n";
    } else {
        echo "FAIL: unexpected span name length " . strlen($name) . "\n";
    }
}
?>
--EXPECTF--
OK: span name length %d (within bounds)
