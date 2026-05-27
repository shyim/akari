--TEST--
Akari\log flushes and compacts so the buffer stays bounded under high volume
--SKIPIF--
<?php include __DIR__ . '/_skipif.inc'; ?>
--INI--
akari.enable=1
--FILE--
<?php
// Emit many more records than the flush threshold (512). Each flush exports the
// buffered records and compacts them away, so the live buffer never grows to
// the full count — it holds only the unsent tail since the last flush.
for ($i = 0; $i < 2000; $i++) {
    Akari\log('info', "msg $i", ['i' => $i]);
}

$data = json_decode(Akari\getLogsJson(), true);
$records = $data['resourceLogs'][0]['scopeLogs'][0]['logRecords'];
$live = count($records);

echo 'logged: 2000' . "\n";
echo 'live buffered < 2000: ' . ($live < 2000 ? 'yes' : 'no') . "\n";
echo 'live buffered <= flush threshold: ' . ($live <= 512 ? 'yes' : 'no') . "\n";
?>
--EXPECT--
logged: 2000
live buffered < 2000: yes
live buffered <= flush threshold: yes
