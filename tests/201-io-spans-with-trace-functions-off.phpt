--TEST--
I/O spans are traced even with trace_functions=0, user functions are not
--SKIPIF--
<?php
include __DIR__ . '/_skipif.inc';
if (!extension_loaded('pdo_sqlite')) die('skip pdo_sqlite not available');
?>
--INI--
akari.enable=1
akari.trace_functions=0
--FILE--
<?php
function myWrapper() {
    $pdo = new PDO('sqlite::memory:');
    $pdo->query('SELECT 1');
    usleep(100);
    return 42;
}

myWrapper();

$json = Akari\getSpansJson();
$data = json_decode($json, true);
$spans = $data['resourceSpans'][0]['scopeSpans'][0]['spans'];
$names = array_column($spans, 'name');

// User function should NOT be traced
echo "has myWrapper: " . (in_array('myWrapper', $names) ? 'yes' : 'no') . "\n";
// But I/O should be
echo "has PDO::query: " . (in_array('PDO::query', $names) ? 'yes' : 'no') . "\n";
echo "has usleep: " . (in_array('usleep', $names) ? 'yes' : 'no') . "\n";
?>
--EXPECT--
has myWrapper: no
has PDO::query: yes
has usleep: yes
