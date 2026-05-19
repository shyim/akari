--TEST--
Shallow max_depth records only the outermost hooked span
--SKIPIF--
<?php include __DIR__ . '/_skipif.inc'; ?>
--INI--
akari.enable=1
akari.max_depth=2
--FILE--
<?php
require __DIR__ . '/_doctrine_mock.inc';

// A two-level nested hooked chain; max_depth=2 must keep only the outer span.
class EM implements \Doctrine\ORM\EntityManagerInterface {
    public function flush($n = 3) {
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
spans: 1
