--TEST--
Deep recursion of hooked calls does not overflow observer stack
--SKIPIF--
<?php include __DIR__ . '/_skipif.inc'; ?>
--INI--
akari.enable=1
akari.max_depth=256
--FILE--
<?php
require __DIR__ . '/_doctrine_mock.inc';

// max_depth=256 = PROFILER_MAX_STACK — this was the exact trigger for OOB
// writes before the stack_push bounds check was added. Recurse through a
// hooked method deeper than the physical stack capacity.
class EM implements \Doctrine\ORM\EntityManagerInterface {
    public function flush($n = 300) {
        if ($n > 0) {
            $this->flush($n - 1);
        } else {
            usleep(1);
        }
    }
}

(new EM())->flush();

// Verify we recorded at most PROFILER_MAX_STACK spans and didn't crash
$count = Akari\getSpanCount();
if ($count > 0 && $count <= 256) {
    echo "ok: $count spans (no OOB crash)\n";
} else {
    echo "unexpected span count: $count\n";
}
?>
--EXPECTF--
ok: %d spans (no OOB crash)
