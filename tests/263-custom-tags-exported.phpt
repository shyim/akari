--TEST--
Userland tags and custom variables are exported as span attributes
--SKIPIF--
<?php
include __DIR__ . '/_skipif.inc';
if (!function_exists('Akari\\getSpansJson')) die('skip requires --enable-akari-debug');
if (!function_exists('socket_create')) die('skip ext-sockets not available');
$probe = socket_create(AF_INET, SOCK_DGRAM, SOL_UDP);
if (!@socket_bind($probe, '127.0.0.1', 14324)) die('skip udp port 14324 busy');
socket_close($probe);
?>
--INI--
akari.enable=0
akari.trace_cli=1
akari.udp_host=127.0.0.1
akari.udp_port=14324
--FILE--
<?php
require __DIR__ . '/_msgpack_decode.inc';

function attributeMap(array $span): array
{
    $result = [];
    foreach ($span['attributes'] as $attribute) {
        $result[$attribute['key']] = $attribute['value']['stringValue'] ?? null;
    }
    return $result;
}

#[Akari\Span(name: 'tagged-child')]
function taggedChild(): void
{
    Akari\addTag('customer.id', '42=vip');
    Akari\addTag('removed.tag', 'not-exported');
    Akari\removeTag('removed.tag');
}

$sock = socket_create(AF_INET, SOCK_DGRAM, SOL_UDP);
if (!socket_bind($sock, '127.0.0.1', 14324)) {
    throw new RuntimeException('failed to bind UDP listener');
}

Akari\enable();
Akari\createSpan('manual-tagged');
Akari\addTag('manual.tag', 'manual-value');
taggedChild();
Akari\setCustomVariable('tenant', 'acme=eu');

$live = json_decode(Akari\getSpansJson(), true);
$debugChild = null;
$debugManual = null;
foreach ($live['resourceSpans'][0]['scopeSpans'][0]['spans'] as $span) {
    if ($span['name'] === 'tagged-child') {
        $debugChild = attributeMap($span);
    } elseif ($span['name'] === 'manual-tagged') {
        $debugManual = attributeMap($span);
    }
}

Akari\disable();

socket_set_option($sock, SOL_SOCKET, SO_RCVTIMEO, ['sec' => 2, 'usec' => 0]);
$buf = '';
$received = socket_recvfrom($sock, $buf, 65535, 0, $from, $fromPort);
socket_close($sock);

$wire = akari_msgpack_decode($buf);
$wireChild = [];
$wireManual = [];
$wireRoot = [];
foreach ($wire['sp'] as $span) {
    $tags = [];
    foreach ($span['ct'] ?? [] as $tag) {
        $tags[$tag['k']] = $tag['v'];
    }
    if ($span['n'] === 'tagged-child') {
        $wireChild = $tags;
    } elseif ($span['n'] === 'manual-tagged') {
        $wireManual = $tags;
    } elseif (isset($span['pv'])) {
        $wireRoot = $tags;
    }
}

$final = json_decode(Akari\getSpansJson(), true);
$debugRoot = attributeMap($final['resourceSpans'][0]['scopeSpans'][0]['spans'][0]);

echo 'debug child tag: ' . (($debugChild['customer.id'] ?? null) === '42=vip' ? 'yes' : 'no') . "\n";
echo 'debug manual tag: ' . (($debugManual['manual.tag'] ?? null) === 'manual-value' ? 'yes' : 'no') . "\n";
echo 'removed tag absent: ' . (!array_key_exists('removed.tag', $debugChild) ? 'yes' : 'no') . "\n";
echo 'debug root custom variable: ' . (($debugRoot['custom.tenant'] ?? null) === 'acme=eu' ? 'yes' : 'no') . "\n";
echo 'UDP received: ' . ($received > 0 ? 'yes' : 'no') . "\n";
echo 'wire child tag: ' . (($wireChild['customer.id'] ?? null) === '42=vip' ? 'yes' : 'no') . "\n";
echo 'wire manual tag: ' . (($wireManual['manual.tag'] ?? null) === 'manual-value' ? 'yes' : 'no') . "\n";
echo 'wire root custom variable: ' . (($wireRoot['custom.tenant'] ?? null) === 'acme=eu' ? 'yes' : 'no') . "\n";
?>
--EXPECT--
debug child tag: yes
debug manual tag: yes
removed tag absent: yes
debug root custom variable: yes
UDP received: yes
wire child tag: yes
wire manual tag: yes
wire root custom variable: yes
