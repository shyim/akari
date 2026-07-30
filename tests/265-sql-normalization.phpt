--TEST--
SQL hooks normalize literal values before exporting db.statement
--SKIPIF--
<?php
include __DIR__ . '/_skipif.inc';
if (!extension_loaded('pdo_sqlite')) die('skip pdo_sqlite not available');
if (!function_exists('Akari\\getSpansJson')) die('skip requires --enable-akari-debug');
?>
--FILE--
<?php
Akari\enable();

$pdo = new PDO('sqlite::memory:');
$pdo->query("SELECT 'customer-secret' AS token, 12345 AS account, 67.89 AS ratio");
$prepared = $pdo->prepare("SELECT 'prepared-secret' AS token, 42 AS account");
$prepared->execute();

$spans = json_decode(Akari\getSpansJson(), true)['resourceSpans'][0]['scopeSpans'][0]['spans'];
$statements = [];
foreach ($spans as $span) {
    foreach ($span['attributes'] as $attribute) {
        if ($attribute['key'] === 'db.statement') {
            $statements[] = $attribute['value']['stringValue'];
        }
    }
}

echo 'query normalized: ' .
    (in_array('SELECT ? AS token, ? AS account, ? AS ratio', $statements, true) ? 'yes' : 'no') . "\n";
echo 'prepared statement normalized: ' .
    (in_array('SELECT ? AS token, ? AS account', $statements, true) ? 'yes' : 'no') . "\n";
echo 'secret absent: ' .
    (!str_contains(json_encode($statements), 'secret') ? 'yes' : 'no') . "\n";

Akari\disable();
?>
--EXPECT--
query normalized: yes
prepared statement normalized: yes
secret absent: yes
