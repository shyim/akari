--TEST--
Akari\log stringifies scalar context values and skips non-scalars
--SKIPIF--
<?php include __DIR__ . '/_skipif.inc'; ?>
--INI--
akari.enable=1
--FILE--
<?php
Akari\log('debug', 'types', [
    'int'    => 7,
    'float'  => 3.5,
    'true'   => true,
    'false'  => false,
    'string' => 'hi',
    'arr'    => [1, 2],      // non-scalar: skipped
    'null'   => null,        // non-scalar: skipped
]);

$data = json_decode(Akari\getLogsJson(), true);
$rec = $data['resourceLogs'][0]['scopeLogs'][0]['logRecords'][0];
$attrs = array_column($rec['attributes'], 'value', 'key');

echo 'int: ' . $attrs['int']['stringValue'] . "\n";
echo 'float: ' . $attrs['float']['stringValue'] . "\n";
echo 'true: ' . $attrs['true']['stringValue'] . "\n";
echo 'false: ' . var_export($attrs['false']['stringValue'], true) . "\n";
echo 'string: ' . $attrs['string']['stringValue'] . "\n";
echo 'arr present: ' . (isset($attrs['arr']) ? 'yes' : 'no') . "\n";
echo 'null present: ' . (isset($attrs['null']) ? 'yes' : 'no') . "\n";
?>
--EXPECT--
int: 7
float: 3.5
true: 1
false: ''
string: hi
arr present: no
null present: no
