--TEST--
Span kinds are correct: CLIENT for I/O, INTERNAL for sleep/session
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
$pdo = new PDO('sqlite::memory:');
$pdo->query('SELECT 1');
usleep(100);
gethostbyname("localhost");

$json = Akari\getSpansJson();
$data = json_decode($json, true);
$spans = $data['resourceSpans'][0]['scopeSpans'][0]['spans'];

$kinds = ['INTERNAL' => 1, 'SERVER' => 2, 'CLIENT' => 3, 'PRODUCER' => 4, 'CONSUMER' => 5];
$kind_names = array_flip($kinds);

foreach ($spans as $span) {
    $name = $span['name'];
    $kind = $kind_names[$span['kind']] ?? 'UNKNOWN(' . $span['kind'] . ')';
    if (in_array($name, ['PDO::query', 'usleep', 'gethostbyname'])) {
        echo "$name: $kind\n";
    }
}
?>
--EXPECT--
PDO::query: CLIENT
usleep: INTERNAL
gethostbyname: CLIENT
