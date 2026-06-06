--TEST--
Akari generated arginfo exposes the expected Reflection signatures
--SKIPIF--
<?php
include __DIR__ . '/_skipif.inc';
if (!function_exists('Akari\getSpansJson')) die('skip akari debug introspection functions not enabled');
?>
--FILE--
<?php
function return_type(string $function): string {
    return (string)(new ReflectionFunction($function))->getReturnType();
}

function parameter_type(string $function, int $parameter): string {
    $reflection = new ReflectionFunction($function);
    return (string)$reflection->getParameters()[$parameter]->getType();
}

$log = new ReflectionFunction('Akari\log');
$context = $log->getParameters()[2];

echo 'enable return: ' . return_type('Akari\enable') . "\n";
echo 'getTransactionName return: ' . return_type('Akari\getTransactionName') . "\n";
echo 'createSpan return: ' . return_type('Akari\createSpan') . "\n";
echo 'logException exception: ' . parameter_type('Akari\logException', 0) . "\n";
echo 'log context type: ' . $context->getType() . "\n";
echo 'log context default empty array: ' . ($context->getDefaultValue() === [] ? 'yes' : 'no') . "\n";
echo 'getSpansJson return: ' . return_type('Akari\getSpansJson') . "\n";
?>
--EXPECT--
enable return: bool
getTransactionName return: ?string
createSpan return: string|false
logException exception: Throwable
log context type: ?array
log context default empty array: yes
getSpansJson return: string|false
