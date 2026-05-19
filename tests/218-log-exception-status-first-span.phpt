--TEST--
Akari\logException marks the current span (index 0) as error
--SKIPIF--
<?php include __DIR__ . '/_skipif.inc'; ?>
--INI--
akari.enable=1
--FILE--
<?php
require __DIR__ . '/_doctrine_mock.inc';

// logException attaches to the current span. Calling it inside the only open
// hooked span (index 0) must mark that span as error.
class EM implements \Doctrine\ORM\EntityManagerInterface {
    public function flush() {
        Akari\logException(new RuntimeException('logged first span failure'));
    }
}

(new EM())->flush();

$json = Akari\getSpansJson();
$data = json_decode($json, true);
$spans = $data['resourceSpans'][0]['scopeSpans'][0]['spans'];
$first = $spans[0] ?? null;

echo 'span 0 status: ' . (($first['status']['code'] ?? null) === 2 ? 'ERROR' : 'not error') . "\n";
echo 'span 0 exception event: ' . (!empty($first['events']) ? 'yes' : 'no') . "\n";
?>
--EXPECT--
span 0 status: ERROR
span 0 exception event: yes
