<?php

function akari_curl_test_server_start(): array
{
    $router = tempnam(sys_get_temp_dir(), 'akari_curl_router_');
    if ($router === false) {
        throw new RuntimeException('failed to create router file');
    }

    $router_code = <<<'PHP'
<?php
$path = parse_url($_SERVER['REQUEST_URI'] ?? '/', PHP_URL_PATH);

if ($path === '/headers') {
    $headers = [];
    foreach ($_SERVER as $key => $value) {
        if (strncmp($key, 'HTTP_', 5) !== 0) {
            continue;
        }
        $name = str_replace(' ', '-', ucwords(strtolower(str_replace('_', ' ', substr($key, 5)))));
        $headers[$name] = $value;
    }

    header('Content-Type: application/json');
    echo json_encode(['headers' => $headers], JSON_UNESCAPED_SLASHES);
    return;
}

if (preg_match('#^/status/([0-9]{3})$#', $path, $m)) {
    http_response_code((int) $m[1]);
    header('Content-Type: text/plain');
    echo 'status ' . $m[1];
    return;
}

http_response_code(404);
header('Content-Type: text/plain');
echo 'not found';
PHP;

    file_put_contents($router, $router_code);

    $socket = stream_socket_server('tcp://127.0.0.1:0', $errno, $errstr, STREAM_SERVER_BIND | STREAM_SERVER_LISTEN);
    if (!$socket) {
        @unlink($router);
        throw new RuntimeException("failed to reserve local port: $errstr");
    }
    $name = stream_socket_get_name($socket, false);
    fclose($socket);

    $port = (int) substr(strrchr($name, ':'), 1);
    if ($port <= 0) {
        @unlink($router);
        throw new RuntimeException('failed to determine local port');
    }

    $descriptors = [
        0 => ['pipe', 'r'],
        1 => ['pipe', 'w'],
        2 => ['pipe', 'w'],
    ];

    $process = proc_open([PHP_BINARY, '-n', '-S', '127.0.0.1:' . $port, $router], $descriptors, $pipes);
    if (!is_resource($process)) {
        @unlink($router);
        throw new RuntimeException('failed to start local PHP server');
    }

    fclose($pipes[0]);
    stream_set_blocking($pipes[1], false);
    stream_set_blocking($pipes[2], false);

    $closed = false;
    $cleanup = static function () use (&$closed, $process, $pipes, $router): void {
        if ($closed) {
            return;
        }
        $closed = true;
        foreach ([1, 2] as $idx) {
            if (isset($pipes[$idx]) && is_resource($pipes[$idx])) {
                fclose($pipes[$idx]);
            }
        }
        if (is_resource($process)) {
            proc_terminate($process);
            proc_close($process);
        }
        @unlink($router);
    };
    register_shutdown_function($cleanup);

    for ($i = 0; $i < 50; $i++) {
        $conn = @stream_socket_client('tcp://127.0.0.1:' . $port, $errno, $errstr, 0.1);
        if ($conn) {
            fclose($conn);
            return ['base_url' => 'http://127.0.0.1:' . $port];
        }
        usleep(100000);
    }

    $cleanup();
    throw new RuntimeException('local PHP server did not start');
}
