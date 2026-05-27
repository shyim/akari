<?php
/**
 * OTLP logs demo with Akari\log().
 *
 * Shows: structured log records emitted from PHP and correlated to the current
 * trace. Each Akari\log() call attaches the request's trace_id and the active
 * span_id, so in Grafana you can jump from a log line straight to its trace
 * (and back). In this web request the active span is the request's root span,
 * so every log below correlates to that trace.
 *
 * Severity is a PSR-3 level string; the context array becomes log attributes.
 */

use function Akari\log;

class Checkout
{
    public function process(string $orderId, int $amountCents): bool
    {
        log('info', 'checkout started', [
            'order_id' => $orderId,
            'amount_cents' => $amountCents,
            'currency' => 'EUR',
        ]);

        $attempt = 0;
        $captured = false;
        while ($attempt < 3 && !$captured) {
            $attempt++;
            // Pretend the first attempt is declined, then it succeeds.
            $captured = $attempt >= 2;

            if (!$captured) {
                log('warning', 'payment declined, retrying', [
                    'order_id' => $orderId,
                    'attempt' => $attempt,
                    'gateway' => 'stripe',
                ]);
            }
        }

        if ($captured) {
            log('info', 'payment captured', [
                'order_id' => $orderId,
                'attempts' => $attempt,
            ]);
        } else {
            log('error', 'payment failed after retries', [
                'order_id' => $orderId,
                'attempts' => $attempt,
            ]);
        }

        return $captured;
    }
}

$checkout = new Checkout();
$ok = $checkout->process('ORD-' . random_int(1000, 9999), 4999);

// A final request-scope log line.
log($ok ? 'notice' : 'error', 'checkout finished', ['success' => $ok]);

echo "<h2>OTLP Logs Demo</h2>";
echo "<p>This request emitted structured log records via <code>Akari\\log()</code>:</p>";
echo "<ul>";
echo "<li><code>info</code> — checkout started (order + amount attributes)</li>";
echo "<li><code>warning</code> — payment declined, retrying (attempt 1)</li>";
echo "<li><code>info</code> — payment captured</li>";
echo "<li><code>" . ($ok ? 'notice' : 'error') . "</code> — checkout finished</li>";
echo "</ul>";
echo "<p>Each record carries the request's <code>trace_id</code> and the active "
   . "<code>span_id</code>, so logs and traces are linked.</p>";
echo "<p><em>Open <a href='http://localhost:3000'>Grafana</a> → Explore → "
   . "<strong>Loki</strong> datasource, query <code>{service_name=\"demo-php-app\"}</code>, "
   . "then click a log line's trace ID to jump to the trace in Tempo.</em></p>";

// For quick inspection without a backend, the in-memory OTLP logs JSON:
echo "<details><summary>Raw OTLP logs JSON (debug)</summary><pre>";
echo htmlspecialchars(json_encode(
    json_decode(Akari\getLogsJson(), true),
    JSON_PRETTY_PRINT | JSON_UNESCAPED_SLASHES
));
echo "</pre></details>";
