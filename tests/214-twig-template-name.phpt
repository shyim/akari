--TEST--
Twig: render span includes template name, displayBlock span includes block name
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
            ob_start();
            $this->display($context);
            return ob_get_clean();
        }

        public function display(array $context = []): void {
            echo "rendered";
        }

        public function displayBlock(string $name, array $context = [], array $blocks = [], bool $useBlocks = true): void {
            echo "block:$name";
        }
    }
}

namespace App {
    class BlogTemplate extends \Twig\Template {
        public function getTemplateName(): string {
            return 'blog/index.html.twig';
        }
    }
}

namespace {
    $tpl = new \App\BlogTemplate();
    ob_start();
    $tpl->render([]);
    $tpl->displayBlock('header', []);
    $tpl->displayBlock('content', []);
    ob_end_clean();

    $json = \Akari\getSpansJson();
    $data = json_decode($json, true);
    $spans = $data['resourceSpans'][0]['scopeSpans'][0]['spans'];

    // Check span names
    $names = array_column($spans, 'name');

    // Template render span should be named "twig.render blog/index.html.twig"
    $has_render = in_array('twig.render blog/index.html.twig', $names);
    echo "render span name: " . ($has_render ? 'twig.render blog/index.html.twig' : 'FAIL: ' . implode(', ', $names)) . "\n";

    // Block spans should be named "twig.block template::block"
    $has_header = in_array('twig.block blog/index.html.twig::header', $names);
    $has_content = in_array('twig.block blog/index.html.twig::content', $names);
    echo "header block: " . ($has_header ? 'yes' : 'no') . "\n";
    echo "content block: " . ($has_content ? 'yes' : 'no') . "\n";

    // Check first render span has db.system=twig attribute
    foreach ($spans as $span) {
        if ($span['name'] === 'twig.render blog/index.html.twig') {
            foreach ($span['attributes'] as $attr) {
                if ($attr['key'] === 'db.system') echo "db.system: " . $attr['value']['stringValue'] . "\n";
                if ($attr['key'] === 'db.statement') echo "db.statement: " . $attr['value']['stringValue'] . "\n";
            }
            break; /* only check first match */
        }
    }
}
?>
--EXPECT--
render span name: twig.render blog/index.html.twig
header block: yes
content block: yes
db.system: twig
db.statement: blog/index.html.twig
