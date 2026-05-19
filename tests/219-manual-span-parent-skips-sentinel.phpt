--TEST--
createSpan parent lookup skips observer sentinels
--SKIPIF--
<?php include __DIR__ . '/_skipif.inc'; ?>
--INI--
akari.enable=1
--FILE--
<?php
require __DIR__ . '/_doctrine_mock.inc';

// flush() is hooked (real span). It calls an unhooked helper that pushes a
// sentinel onto the observer stack before creating a manual span. The manual
// span's parent lookup must skip the sentinel and resolve to flush.
function manual_parent_probe() {
    Akari\createSpan('manual-child');
}

class EM implements \Doctrine\ORM\EntityManagerInterface {
    public function flush() {
        manual_parent_probe();
    }
}

(new EM())->flush();

$json = Akari\getSpansJson();
$data = json_decode($json, true);
$spans = $data['resourceSpans'][0]['scopeSpans'][0]['spans'];

$parent = null;
$manual = null;
foreach ($spans as $span) {
    if ($span['name'] === 'EM::flush') {
        $parent = $span;
    }
    if ($span['name'] === 'manual-child') {
        $manual = $span;
    }
}

echo 'has parent span: ' . ($parent ? 'yes' : 'no') . "\n";
echo 'has manual span: ' . ($manual ? 'yes' : 'no') . "\n";
echo 'manual parent matches: ' . (
    $parent && $manual && ($manual['parentSpanId'] ?? null) === $parent['spanId']
        ? 'yes'
        : 'no'
) . "\n";
?>
--EXPECT--
has parent span: yes
has manual span: yes
manual parent matches: yes
