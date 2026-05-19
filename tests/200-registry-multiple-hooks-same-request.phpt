--TEST--
Registry: multiple different hook types in single request produce correct spans
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
// Mix of different hook types in one request
$pdo = new PDO('sqlite::memory:');
$pdo->exec('CREATE TABLE t (id INTEGER)');

usleep(1000);
gethostbyname("localhost");
$pdo->query('SELECT 1');

$json = Akari\getSpansJson();
$data = json_decode($json, true);
$spans = $data['resourceSpans'][0]['scopeSpans'][0]['spans'];
$names = array_column($spans, 'name');

echo "has usleep: " . (in_array('usleep', $names) ? 'yes' : 'no') . "\n";
echo "has gethostbyname: " . (in_array('gethostbyname', $names) ? 'yes' : 'no') . "\n";
echo "has PDO::exec: " . (in_array('PDO::exec', $names) ? 'yes' : 'no') . "\n";
echo "has PDO::query: " . (in_array('PDO::query', $names) ? 'yes' : 'no') . "\n";
echo "span count >= 4: " . (count($spans) >= 4 ? 'yes' : 'no') . "\n";
?>
--EXPECT--
has usleep: yes
has gethostbyname: yes
has PDO::exec: yes
has PDO::query: yes
span count >= 4: yes
