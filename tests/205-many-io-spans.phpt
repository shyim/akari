--TEST--
Many I/O spans from different hooks in a single request
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
// Generate spans from multiple hook types
$pdo = new PDO('sqlite::memory:');
$pdo->exec('CREATE TABLE t (id INTEGER)');

for ($i = 0; $i < 10; $i++) {
    $pdo->exec("INSERT INTO t VALUES ($i)");
}
$pdo->query('SELECT * FROM t');

usleep(100);
usleep(100);
gethostbyname("localhost");

$json = Akari\getSpansJson();
$data = json_decode($json, true);
$spans = $data['resourceSpans'][0]['scopeSpans'][0]['spans'];

// Count by type
$counts = [];
foreach ($spans as $span) {
    $name = $span['name'];
    $counts[$name] = ($counts[$name] ?? 0) + 1;
}

echo "PDO::exec count: " . ($counts['PDO::exec'] ?? 0) . "\n";
echo "PDO::query count: " . ($counts['PDO::query'] ?? 0) . "\n";
echo "usleep count: " . ($counts['usleep'] ?? 0) . "\n";
echo "gethostbyname count: " . ($counts['gethostbyname'] ?? 0) . "\n";
echo "total >= 15: " . (count($spans) >= 15 ? 'yes' : 'no') . "\n";
?>
--EXPECT--
PDO::exec count: 11
PDO::query count: 1
usleep count: 2
gethostbyname count: 1
total >= 15: yes
