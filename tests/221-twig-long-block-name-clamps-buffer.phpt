--TEST--
Twig: long displayBlock names clamp db.statement to the local buffer
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
        echo "statement clamped: no\n";
        echo "statement prefix: no\n";
        exit;
    }

    $statement = null;
    foreach ($block_span['attributes'] as $attr) {
        if ($attr['key'] === 'db.statement') {
            $statement = $attr['value']['stringValue'];
            break;
        }
    }

    $name_len = strlen($block_span['name']);
    $statement_len = $statement === null ? 0 : strlen($statement);
    $prefix = 'long-template.html.twig::';

    echo "json valid: yes\n";
    echo "span name clamped: " . (($name_len > 0 && $name_len <= 255) ? 'yes' : 'no') . "\n";
    echo "statement clamped: " . (($statement_len > 0 && $statement_len <= 511) ? 'yes' : 'no') . "\n";
    echo "statement prefix: " . (($statement !== null && strncmp($statement, $prefix, strlen($prefix)) === 0) ? 'yes' : 'no') . "\n";
}
?>
--EXPECT--
json valid: yes
span name clamped: yes
statement clamped: yes
statement prefix: yes
