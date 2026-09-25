--TEST--
AMQPExchange::publish keeps existing headers when headers is a reference (#26)
--SKIPIF--
<?php
include __DIR__ . '/_skipif.inc';
if (!class_exists('AMQPConnection')) die('skip ext-amqp not available');
try {
    $conn = new AMQPConnection([
        'host' => getenv('AKARI_AMQP_HOST') ?: '127.0.0.1',
        'port' => (int) (getenv('AKARI_AMQP_PORT') ?: 5672),
        'login' => getenv('AKARI_AMQP_USER') ?: 'guest',
        'password' => getenv('AKARI_AMQP_PASS') ?: 'guest',
        'vhost' => '/',
        'connect_timeout' => 2,
        'read_timeout' => 2,
        'write_timeout' => 2,
    ]);
    $conn->connect();
    $conn->disconnect();
} catch (Throwable $e) {
    die('skip RabbitMQ not available: ' . $e->getMessage());
}
?>
--INI--
akari.enable=1
akari.trace_functions=0
--FILE--
<?php
// Mirrors open-telemetry's propagator: inject() writes the carrier by reference,
// which leaves $attributes['headers'] as an IS_REFERENCE. The publish hook must
// keep those headers, add traceparent on the message, and leave the caller alone.
function inject_headers(array &$carrier): void {
    $carrier['foo'] = 'bar';
}

function amqp_connection(): AMQPConnection {
    $conn = new AMQPConnection([
        'host' => getenv('AKARI_AMQP_HOST') ?: '127.0.0.1',
        'port' => (int) (getenv('AKARI_AMQP_PORT') ?: 5672),
        'login' => getenv('AKARI_AMQP_USER') ?: 'guest',
        'password' => getenv('AKARI_AMQP_PASS') ?: 'guest',
        'vhost' => '/',
        'connect_timeout' => 2,
        'read_timeout' => 2,
        'write_timeout' => 2,
    ]);
    $conn->connect();
    return $conn;
}

function assert_caller(string $label, array $headers, array $expected): void {
    ksort($headers);
    ksort($expected);
    if ($headers !== $expected || isset($headers['traceparent'])) {
        echo "FAIL: $label caller mutated: " . json_encode($headers) . "\n";
        return;
    }
    echo "OK: $label caller\n";
}

function assert_message(string $label, ?AMQPEnvelope $msg, array $expected): void {
    if (!$msg) {
        echo "FAIL: $label missing message\n";
        return;
    }
    if ($msg->getBody() !== $label) {
        echo "FAIL: $label body got " . $msg->getBody() . "\n";
        return;
    }
    $headers = $msg->getHeaders();
    foreach ($expected as $key => $value) {
        if (($headers[$key] ?? null) !== $value) {
            echo "FAIL: $label header $key got " . json_encode($headers) . "\n";
            return;
        }
    }
    $traceparent = $headers['traceparent'] ?? '';
    if (!is_string($traceparent) || !preg_match('/^00-[0-9a-f]{32}-[0-9a-f]{16}-01$/', $traceparent)) {
        echo "FAIL: $label traceparent got " . json_encode($headers) . "\n";
        return;
    }
    if (count($headers) !== count($expected) + 1) {
        echo "FAIL: $label unexpected headers " . json_encode($headers) . "\n";
        return;
    }
    echo "OK: $label message\n";
}

function take(AMQPQueue $queue): ?AMQPEnvelope {
    $deadline = microtime(true) + 5;
    do {
        $msg = $queue->get(AMQP_AUTOACK);
        if ($msg instanceof AMQPEnvelope) {
            return $msg;
        }
        usleep(20000);
    } while (microtime(true) < $deadline);
    return null;
}

$conn = amqp_connection();
$channel = new AMQPChannel($conn);
$queue = new AMQPQueue($channel);
$queue->setName('akari_hdr_' . getmypid() . '_' . bin2hex(random_bytes(4)));
$queue->setFlags(AMQP_EXCLUSIVE);
$queue->declareQueue();

$exchange = new AMQPExchange($channel);
$routingKey = $queue->getName();

$plain = ['headers' => ['type' => 'Msg']];
$exchange->publish('plain', $routingKey, AMQP_NOPARAM, $plain);
assert_caller('plain', $plain['headers'], ['type' => 'Msg']);

$referenced = ['headers' => ['type' => 'Msg']];
inject_headers($referenced['headers']);
$exchange->publish('ref', $routingKey, AMQP_NOPARAM, $referenced);
assert_caller('ref', $referenced['headers'], ['type' => 'Msg', 'foo' => 'bar']);

$exchange->publish('literal', $routingKey, AMQP_NOPARAM, ['headers' => ['type' => 'Lit']]);

assert_message('plain', take($queue), ['type' => 'Msg']);
assert_message('ref', take($queue), ['type' => 'Msg', 'foo' => 'bar']);
assert_message('literal', take($queue), ['type' => 'Lit']);

$conn->disconnect();
?>
--EXPECT--
OK: plain caller
OK: ref caller
OK: plain message
OK: ref message
OK: literal message
