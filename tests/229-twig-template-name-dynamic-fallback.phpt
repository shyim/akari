--TEST--
Twig: a getTemplateName() that is not a single constant return reports <unknown>
--SKIPIF--
<?php include __DIR__ . '/_skipif.inc'; ?>
--INI--
akari.enable=1
akari.trace_functions=0
--FILE--
<?php
// The template name is read straight from getTemplateName()'s compiled opcodes
// (a single `return "name";`). When the method computes the name at runtime or
// returns conditionally, the opcode scan cannot resolve a single constant. We
// deliberately do NOT invoke the method on the hot path; the span is reported
// with template.name "<unknown>" rather than a guessed name.
namespace Twig {
    abstract class Template {
        abstract public function getTemplateName(): string;
        public function render(array $context = []): string {
            ob_start();
            $this->display($context);
            return ob_get_clean();
        }
        public function display(array $context = []): void { echo "rendered"; }
    }
}

namespace App {
    class DynamicTemplate extends \Twig\Template {
        public function getTemplateName(): string {
            $parts = ['dyn', 'page'];
            return implode('/', $parts) . '.html.twig';
        }
    }
    class ConditionalTemplate extends \Twig\Template {
        public function getTemplateName(): string {
            if (mt_rand(0, 1) === 2) return 'never.html.twig';
            return 'also-never.html.twig';
        }
    }
}

namespace {
    (new \App\DynamicTemplate())->render([]);
    (new \App\ConditionalTemplate())->render([]);

    $data = json_decode(\Akari\getSpansJson(), true);
    $spans = $data['resourceSpans'][0]['scopeSpans'][0]['spans'];
    $names = array_column($spans, 'name');

    // Both unresolved templates render under the unknown name. render() and the
    // display() it calls are both hooked, so each of the two templates yields
    // two "twig.render <unknown>" spans.
    $unknown = 0;
    foreach ($names as $n) {
        if ($n === 'twig.render <unknown>') $unknown++;
    }
    echo 'unknown render spans: ' . $unknown . "\n";

    // The guessed branch literals must never leak into a span name.
    echo 'leaked guessed name: ' . (
        in_array('twig.render also-never.html.twig', $names, true) ||
        in_array('twig.render dyn/page.html.twig', $names, true)
        ? 'yes' : 'no') . "\n";

    // template.name attribute on an unknown span is "<unknown>".
    $attr_unknown = 'no';
    foreach ($spans as $span) {
        if ($span['name'] !== 'twig.render <unknown>') continue;
        foreach ($span['attributes'] as $attr) {
            if ($attr['key'] === 'template.name' && $attr['value']['stringValue'] === '<unknown>') {
                $attr_unknown = 'yes';
            }
        }
        break;
    }
    echo 'template.name unknown: ' . $attr_unknown . "\n";
}
?>
--EXPECT--
unknown render spans: 4
leaked guessed name: no
template.name unknown: yes
