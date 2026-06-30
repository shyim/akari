--TEST--
#[Akari\Span] attribute creates spans, with name and minDurationMs support
--SKIPIF--
<?php
include __DIR__ . '/_skipif.inc';
if (!function_exists('Akari\\getSpansJson')) die('skip requires --enable-akari-debug');
?>
--INI--
akari.flush_threshold=100000
--FILE--
<?php
namespace App;

use Akari\Span;

class Svc {
    #[Span]
    public function plain(): int { return 1; }

    #[Span(name: "named-op")]
    public function named(): int { return 2; }

    #[Span(minDurationMs: 50.0)]
    public function fast(): int { return 3; }            // well under 50ms -> dropped

    #[Span(minDurationMs: 1.0)]
    public function slow(): int { usleep(3000); return 4; } // ~3ms -> kept

    public function unmarked(): int { return 5; }
}

// The attribute is a real, reflectable class.
$rm = new \ReflectionClass(Svc::class);
$attr = $rm->getMethod('named')->getAttributes(Span::class)[0]->newInstance();
echo "reflect.name=" . $attr->name . "\n";
echo "reflect.minDurationMs=" . $attr->minDurationMs . "\n";

\Akari\enable();
$s = new Svc();
for ($i = 0; $i < 2; $i++) {
    $s->plain();
    $s->named();
    $s->fast();
    $s->slow();
    $s->unmarked();
}
$spans = json_decode(\Akari\getSpansJson(), true)['resourceSpans'][0]['scopeSpans'][0]['spans'];
\Akari\disable();

$counts = [];
foreach ($spans as $sp) {
    $counts[$sp['name']] = ($counts[$sp['name']] ?? 0) + 1;
}

printf("plain=%d\n",     $counts['App\\Svc::plain'] ?? 0);
printf("named-op=%d\n",  $counts['named-op'] ?? 0);
printf("fast=%d\n",      $counts['App\\Svc::fast'] ?? 0);
printf("slow=%d\n",      $counts['App\\Svc::slow'] ?? 0);
printf("unmarked=%d\n",  $counts['App\\Svc::unmarked'] ?? 0);
?>
--EXPECT--
reflect.name=named-op
reflect.minDurationMs=0
plain=2
named-op=2
fast=0
slow=2
unmarked=0
