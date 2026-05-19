--TEST--
I/O spans coexist with user function spans when trace_functions=1
--SKIPIF--
<?php
include __DIR__ . '/_skipif.inc';
if (!extension_loaded('pdo_sqlite')) die('skip pdo_sqlite not available');
?>
--INI--
akari.enable=1
akari.trace_functions=1
--FILE--
<?php
function myWrapper() {
    $pdo = new PDO('sqlite::memory:');
    $pdo->query('SELECT 1');
    return 42;
}

myWrapper();

$json = Akari\getSpansJson();
$data = json_decode($json, true);
$spans = $data['resourceSpans'][0]['scopeSpans'][0]['spans'];
$names = array_column($spans, 'name');

// Both user function AND I/O should be traced
echo "has myWrapper: " . (in_array('myWrapper', $names) ? 'yes' : 'no') . "\n";
echo "has PDO::query: " . (in_array('PDO::query', $names) ? 'yes' : 'no') . "\n";
?>
--EXPECT--
has myWrapper: yes
has PDO::query: yes
