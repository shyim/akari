<?php
include __DIR__ . '/_skipif.inc';
if (!function_exists('curl_init')) die('skip curl not available');
if (!function_exists('proc_open')) die('skip proc_open not available');
if (!function_exists('stream_socket_server')) die('skip stream_socket_server not available');
if (!function_exists('stream_socket_client')) die('skip stream_socket_client not available');
