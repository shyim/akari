--TEST--
Twig: TemplateWrapper::render extracts template name
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

    // TemplateWrapper::render should be named "twig.render page/show.html.twig"
    $has_wrapper = in_array('twig.render page/show.html.twig', $names);
    echo "wrapper span name: " . ($has_wrapper ? 'twig.render page/show.html.twig' : 'FAIL: ' . implode(', ', $names)) . "\n";

    // Inner Template::render should also be named "twig.render page/show.html.twig"
    $count = 0;
    foreach ($names as $n) {
        if ($n === 'twig.render page/show.html.twig') $count++;
    }
    echo "render span count: $count\n";
}
?>
--EXPECT--
wrapper span name: twig.render page/show.html.twig
render span count: 2
