--TEST--
Sampler: parentbased_always_off traces when the parent is sampled (TRACEPARENT env, CLI)
--SKIPIF--
<?php include __DIR__ . '/_skipif.inc'; ?>
--ENV--
TRACEPARENT=00-11111111111111111111111111111111-2222222222222222-01
--INI--
akari.enable=1
akari.traces_sampler=parentbased_always_off
--FILE--
<?php
usleep(1);

$json = Akari\getSpansJson();
$data = json_decode($json, true);
$spans = $data['resourceSpans'][0]['scopeSpans'][0]['spans'] ?? [];
echo 'spans: ' . (count($spans) > 0 ? 'some' : 'none') . "\n";
echo 'traceId adopted: '
   . ($spans[0]['traceId'] === '11111111111111111111111111111111' ? 'yes' : 'no') . "\n";

$headers = Akari\generateDistributedTracingHeaders();
$parts = explode('-', $headers['traceparent']);
echo 'propagated traceId matches: '
   . ($parts[1] === '11111111111111111111111111111111' ? 'yes' : 'no') . "\n";
?>
--EXPECT--
spans: some
traceId adopted: yes
propagated traceId matches: yes
