--TEST--
#[Akari\Span] duration threshold drops a parent while preserving nested spans
--SKIPIF--
<?php
include __DIR__ . '/_skipif.inc';
if (!function_exists('Akari\\getSpansJson')) die('skip requires --enable-akari-debug');
?>
--INI--
akari.flush_threshold=100000
--FILE--
<?php
use Akari\Span;

#[Span(name: 'threshold-parent', minDurationMs: 100.0)]
function thresholdParent(): void
{
    $active = json_decode(Akari\getSpansJson(), true);
    foreach ($active['resourceSpans'][0]['scopeSpans'][0]['spans'] as $span) {
        if ($span['name'] === 'threshold-parent') {
            $GLOBALS['thresholdParentId'] = $span['spanId'];
        }
    }

    usleep(2000);
}

Akari\enable();
thresholdParent();
$spans = json_decode(Akari\getSpansJson(), true)['resourceSpans'][0]['scopeSpans'][0]['spans'];

$parentPresent = false;
$sleep = null;
foreach ($spans as $span) {
    if ($span['name'] === 'threshold-parent') {
        $parentPresent = true;
    } elseif ($span['name'] === 'usleep') {
        $sleep = $span;
    }
}

$depth = null;
foreach ($sleep['attributes'] as $attribute) {
    if ($attribute['key'] === 'code.stacktrace.depth') {
        $depth = $attribute['value']['intValue'];
    }
}

echo 'threshold parent dropped: ' . (!$parentPresent ? 'yes' : 'no') . "\n";
echo 'nested span preserved: ' . ($sleep !== null ? 'yes' : 'no') . "\n";
echo 'nested span reparented: ' .
    ($sleep['parentSpanId'] !== $GLOBALS['thresholdParentId'] ? 'yes' : 'no') . "\n";
echo 'nested span depth compacted: ' . ($depth === '0' ? 'yes' : 'no') . "\n";

Akari\disable();
?>
--EXPECT--
threshold parent dropped: yes
nested span preserved: yes
nested span reparented: yes
nested span depth compacted: yes
