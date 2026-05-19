--TEST--
All Akari functions exist
--SKIPIF--
<?php include __DIR__ . '/_skipif.inc'; ?>
--FILE--
<?php
$functions = [
    'Akari\enable',
    'Akari\disable',
    'Akari\getSpanCount',
    'Akari\getFrameCount',
    'Akari\getSpansJson',
];

foreach ($functions as $fn) {
    echo $fn . ': ' . (function_exists('\\' . $fn) ? 'yes' : 'no') . "\n";
}
?>
--EXPECT--
Akari\enable: yes
Akari\disable: yes
Akari\getSpanCount: yes
Akari\getFrameCount: yes
Akari\getSpansJson: yes
