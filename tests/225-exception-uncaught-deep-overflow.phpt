--TEST--
Exception event: an uncaught exception is still promoted when the stack overflowed
--SKIPIF--
<?php include __DIR__ . '/_skipif.inc'; ?>
--INI--
akari.enable=1
--FILE--
<?php
require __DIR__ . '/_doctrine_mock.inc';

// Recurse the hooked flush() far deeper than PROFILER_MAX_STACK (256) so the
// observer's span-push overflows and later pops take the overflow-drop path in
// observer_fcall_end. An uncaught exception thrown from the bottom must still
// be recorded and promoted to an error — the overflow path must not swallow
// exception resolution.
class EM implements \Doctrine\ORM\EntityManagerInterface {
    public $depth;
    public function __construct($d) { $this->depth = $d; }
    public function flush() {
        if ($this->depth > 0) { (new EM($this->depth - 1))->flush(); return; }
        throw new RuntimeException('deep uncaught');
    }
}

register_shutdown_function(function () {
    $json = Akari\getSpansJson();
    $data = json_decode($json, true);
    $spans = $data['resourceSpans'][0]['scopeSpans'][0]['spans'] ?? [];

    $found = false;
    foreach ($spans as $s) {
        foreach ($s['events'] ?? [] as $ev) {
            $attr = array_column($ev['attributes'] ?? [], 'value', 'key');
            if (($attr['exception.message']['stringValue'] ?? '') === 'deep uncaught'
                && ($s['status']['code'] ?? 0) === 2) {
                $found = true;
            }
        }
    }
    echo 'deep uncaught promoted: ' . ($found ? 'yes' : 'no') . "\n";
});

(new EM(300))->flush();
?>
--EXPECTF--
%AFatal error: Uncaught RuntimeException: deep uncaught%a
deep uncaught promoted: yes
