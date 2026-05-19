<?php
/**
 * Simple function tracing demo.
 * Shows: user functions, class methods, closures, nested calls.
 */

function fibonacci(int $n): int {
    if ($n <= 1) return $n;
    return fibonacci($n - 1) + fibonacci($n - 2);
}

class Greeter {
    public function greet(string $name): string {
        return $this->format("Hello, " . strtoupper($name) . "!");
    }

    private function format(string $message): string {
        return "<p>{$message}</p>";
    }
}

$transform = fn(string $s) => str_repeat($s, 3);

// Generate some traced calls
$fib = fibonacci(5);
$greeter = new Greeter();
$greeting = $greeter->greet("World");
$transformed = $transform("abc");

echo "<h2>Function Tracing Demo</h2>";
echo $greeting;
echo "<p>fibonacci(5) = {$fib}</p>";
echo "<p>transform('abc') = {$transformed}</p>";
echo "<p><em>Check <a href='http://localhost:16686'>Jaeger</a> for traces — look for service 'demo-php-app'</em></p>";
