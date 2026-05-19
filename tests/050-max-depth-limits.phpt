--TEST--
max_depth caps the number of nested hooked spans on a deep chain
--SKIPIF--
<?php include __DIR__ . '/_skipif.inc'; ?>
--INI--
akari.enable=1
akari.max_depth=4
--FILE--
<?php
require __DIR__ . '/_doctrine_mock.inc';

// Recursive hooked method: each call nests one more EntityManager::flush span.
// The chain is 8 deep, but max_depth=4 must cap recorded spans below that.
class EM implements \Doctrine\ORM\EntityManagerInterface {
    public function flush($n = 8) {
        if ($n > 0) {
            $this->flush($n - 1);
        } else {
            usleep(1);
        }
    }
}

(new EM())->flush();

$count = Akari\getSpanCount();
echo "spans: $count\n";
?>
--EXPECT--
spans: 3
