--TEST--
Engine hooks: compile and GC profiling preserve span state
--SKIPIF--
<?php include __DIR__ . '/_skipif.inc'; ?>
--INI--
akari.enable=1
akari.trace_functions=0
akari.trace_internal=1
akari.trace_compile=1
akari.trace_gc=1
--FILE--
<?php
$path = tempnam(sys_get_temp_dir(), 'akari_engine_hook_');
$prefix = 'akari_engine_hook_' . getmypid() . '_';
$code = "<?php\n";
for ($i = 0; $i < 800; $i++) {
    $code .= "function {$prefix}{$i}() { return {$i}; }\n";
}
file_put_contents($path, $code);
include $path;
unlink($path);

gc_enable();
$cycles = [];
for ($i = 0; $i < 1000; $i++) {
    $node = [];
    $node['self'] = &$node;
    $cycles[] = $node;
}
unset($cycles, $node);
gc_collect_cycles();

usleep(100);

$json = Akari\getSpansJson();
$data = json_decode($json, true);

echo "valid json: " . (is_array($data) ? 'yes' : 'no') . "\n";
$spans = $data['resourceSpans'][0]['scopeSpans'][0]['spans'] ?? [];
echo "has spans: " . (count($spans) > 0 ? 'yes' : 'no') . "\n";
?>
--EXPECT--
valid json: yes
has spans: yes
