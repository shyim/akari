--TEST--
Shopware Profiler::trace span name includes the trace title
--SKIPIF--
<?php include __DIR__ . '/_skipif.inc'; ?>
--INI--
akari.enable=1
akari.trace_functions=0
--FILE--
<?php
namespace Shopware\Core\Profiling {
    class Profiler {
        public static function trace(string $name, \Closure $closure, string $category = 'shopware', array $tags = []) {
            return $closure();
        }
    }
}

namespace {
    $result = \Shopware\Core\Profiling\Profiler::trace('navigation-page-loader', static fn () => 42);
    echo 'result: ' . $result . "\n";

    $json = \Akari\getSpansJson();
    $data = json_decode($json, true);
    $spans = $data['resourceSpans'][0]['scopeSpans'][0]['spans'];
    $names = array_column($spans, 'name');

    echo 'has title span: '
        . (in_array('shopware.profiler navigation-page-loader', $names, true) ? 'yes' : 'no')
        . "\n";
    echo 'has generic span: '
        . (in_array('Shopware\\Core\\Profiling\\Profiler::trace', $names, true) ? 'yes' : 'no')
        . "\n";
}
?>
--EXPECT--
result: 42
has title span: yes
has generic span: no
