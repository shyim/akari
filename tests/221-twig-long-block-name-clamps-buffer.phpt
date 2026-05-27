--TEST--
Twig: long displayBlock names clamp template.block_name to the buffer
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

        public function displayBlock(string $name, array $context = [], array $blocks = [], bool $useBlocks = true): void {
            echo "block";
        }
    }
}

namespace App {
    class LongTemplate extends \Twig\Template {
        public function getTemplateName(): string {
            return 'long-template.html.twig';
        }
    }
}

namespace {
    $tpl = new \App\LongTemplate();
    $block = str_repeat('x', 900);

    ob_start();
    $tpl->displayBlock($block, []);
    ob_end_clean();

    $json = \Akari\getSpansJson();
    $data = json_decode($json, true);

    if (!is_array($data)) {
        echo "json valid: no\n";
        exit;
    }

    $spans = $data['resourceSpans'][0]['scopeSpans'][0]['spans'];
    $block_span = null;
    foreach ($spans as $span) {
        if (str_starts_with($span['name'], 'twig.block ')) {
            $block_span = $span;
            break;
        }
    }

    if (!$block_span) {
        echo "json valid: yes\n";
        echo "span name clamped: no\n";
        echo "block clamped: no\n";
        echo "template name: no\n";
        exit;
    }

    $block = null;
    $tpl_name = null;
    foreach ($block_span['attributes'] as $attr) {
        if ($attr['key'] === 'template.block_name') $block = $attr['value']['stringValue'];
        if ($attr['key'] === 'template.name') $tpl_name = $attr['value']['stringValue'];
    }

    $name_len = strlen($block_span['name']);
    $block_len = $block === null ? 0 : strlen($block);

    echo "json valid: yes\n";
    echo "span name clamped: " . (($name_len > 0 && $name_len <= 255) ? 'yes' : 'no') . "\n";
    echo "block clamped: " . (($block_len > 0 && $block_len <= 127) ? 'yes' : 'no') . "\n";
    echo "template name: " . ($tpl_name === 'long-template.html.twig' ? 'yes' : 'no') . "\n";
}
?>
--EXPECT--
json valid: yes
span name clamped: yes
block clamped: yes
template name: yes
