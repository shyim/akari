--TEST--
Export produces valid JSON with resourceSpans/scopeSpans/spans structure
--SKIPIF--
<?php require __DIR__ . '/_skipif.inc'; ?>
--INI--
akari.enable=1
akari.service_name=test-svc
--FILE--
<?php
// Any hooked span is enough to exercise the OTLP envelope.
usleep(1);

$json = Akari\getSpansJson();
$data = json_decode($json, true);

if (!is_array($data)) {
    echo "FAIL: not valid JSON\n";
} elseif (!isset($data['resourceSpans'])) {
    echo "FAIL: missing resourceSpans\n";
} elseif (!isset($data['resourceSpans'][0]['scopeSpans'])) {
    echo "FAIL: missing scopeSpans\n";
} elseif (!isset($data['resourceSpans'][0]['scopeSpans'][0]['spans'])) {
    echo "FAIL: missing spans\n";
} elseif (count($data['resourceSpans'][0]['scopeSpans'][0]['spans']) < 1) {
    echo "FAIL: spans array is empty\n";
} else {
    echo "OK: valid OTLP JSON structure\n";
}
?>
--EXPECT--
OK: valid OTLP JSON structure
