<?php

declare(strict_types=1);

if (!extension_loaded('akari')) {
    throw new RuntimeException('Build the Akari extension before running benchmarks.');
}

// Fixed inputs and checksums keep runs comparable and catch incomplete work.
function calculateTotal(int $value): int
{
    return ($value * 17 + 23) % 1000;
}

#[Akari\Span(name: 'checkout.calculate')]
function tracedCalculation(int $value): int
{
    return calculateTotal($value);
}

$workload = $argv[1] ?? '';
$checksum = 0;
switch ($workload) {
    case 'calls':
        // Identical work with tracing disabled, sampled out, and enabled.
        // These functions have no hooks: this measures observer lookup overhead.
        for ($i = 0; $i < 1_000_000; ++$i) {
            $checksum += calculateTotal($i);
        }
        $expected = 499_500_000;
        break;

    case 'spans':
        // Repeated frames exercise deduplication, completed spans, and flushing.
        for ($i = 0; $i < 20_000; ++$i) {
            $checksum += tracedCalculation($i);
        }
        $expected = 9_990_000;
        break;

    case 'logs':
        $context = ['tenant' => 'acme', 'attempt' => 2, 'cached' => true];
        for ($i = 0; $i < 10_000; ++$i) {
            Akari\log('warning', 'Payment gateway retry scheduled', $context);
            ++$checksum;
        }
        $expected = 10_000;
        break;

    case 'sqlite':
        if (!class_exists(SQLite3::class)) {
            throw new RuntimeException('The PHP benchmark runtime requires SQLite3 support.');
        }
        $db = new SQLite3(':memory:');
        $db->enableExceptions(true);
        $db->exec('CREATE TABLE products (id INTEGER PRIMARY KEY, name TEXT, price INTEGER)');
        $db->exec("INSERT INTO products VALUES (1, 'Akari', 42)");
        for ($i = 0; $i < 5_000; ++$i) {
            $checksum += $db->querySingle('SELECT price FROM products WHERE id = 1');
        }
        $db->close();
        $expected = 210_000;
        break;

    default:
        throw new InvalidArgumentException("Unknown workload: $workload");
}

if ($checksum !== $expected) {
    throw new RuntimeException("Unexpected checksum: $checksum (expected $expected)");
}

// Include final serialization and cleanup in the measured command.
Akari\disable();
echo "$workload: $checksum\n";
