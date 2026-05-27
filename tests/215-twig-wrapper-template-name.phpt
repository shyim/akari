--TEST--
Twig: TemplateWrapper is not hooked; only the inner compiled template is instrumented
--SKIPIF--
<?php include __DIR__ . '/_skipif.inc'; ?>
--INI--
akari.enable=1
akari.trace_functions=0
--FILE--
<?php
namespace Twig {
    abstract class Template {
        abstract public function getTemplateName(): string;
        public function render(array $context = []): string {
            return "rendered";
        }
        public function display(array $context = []): void {
            echo "rendered";
        }
    }

    class TemplateWrapper {
        private Template $template;
        public function __construct(Template $template) {
            $this->template = $template;
        }
        public function getTemplateName(): string {
            return $this->template->getTemplateName();
        }
        public function render(array $context = []): string {
            return $this->template->render($context);
        }
    }
}

namespace App {
    class PageTemplate extends \Twig\Template {
        public function getTemplateName(): string {
            return 'page/show.html.twig';
        }
    }
}

namespace {
    $tpl = new \App\PageTemplate();
    $wrapper = new \Twig\TemplateWrapper($tpl);
    $wrapper->render([]);

    $json = \Akari\getSpansJson();
    $data = json_decode($json, true);
    $spans = $data['resourceSpans'][0]['scopeSpans'][0]['spans'];

    $names = array_column($spans, 'name');

    // The inner Twig\Template::render resolves its name from the compiled
    // constant return in App\PageTemplate::getTemplateName().
    $has_inner = in_array('twig.render page/show.html.twig', $names);
    echo "inner render resolved: " . ($has_inner ? 'yes' : 'FAIL: ' . implode(', ', $names)) . "\n";

    // TemplateWrapper is not instrumented, so the only render span is the inner
    // one — the delegating wrapper does not add a redundant span.
    $render_spans = 0;
    foreach ($names as $n) {
        if (str_starts_with($n, 'twig.render ')) $render_spans++;
    }
    echo "render span count: $render_spans\n";
}
?>
--EXPECT--
inner render resolved: yes
render span count: 1
