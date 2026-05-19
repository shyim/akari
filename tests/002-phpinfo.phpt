--TEST--
phpinfo() shows akari support and version
--SKIPIF--
<?php include __DIR__ . '/_skipif.inc'; ?>
--FILE--
<?php
ob_start();
phpinfo(INFO_MODULES);
$info = ob_get_clean();

if (strpos($info, 'akari support => enabled') !== false) {
    echo "support: enabled\n";
} else {
    echo "support: NOT FOUND\n";
}

if (strpos($info, 'Version => 0.1.0') !== false) {
    echo "version: 0.1.0\n";
} else {
    echo "version: NOT FOUND\n";
}
?>
--EXPECT--
support: enabled
version: 0.1.0
