#!/usr/bin/env bash
# Run the akari .phpt suite under ASan/UBSan.
#
# Sanitizer env (ASAN_OPTIONS, USE_ZEND_ALLOC, ...) is set in the image.
# Any args passed to `docker run` are treated as a TESTS override, e.g.
#   docker run --rm akari-asan tests/080-otlp-valid-json.phpt
set -euo pipefail

cd /opt/akari

EXT="$(pwd)/modules/akari.so"
PHP_BIN="$(command -v php)"

echo "== akari ASan test run =="
echo "php:        $PHP_BIN"
echo "extension:  $EXT"
echo "ASAN_OPTIONS=$ASAN_OPTIONS"
echo

# Drive run-tests.php the same way the project's `make test` does: point it at
# our debug+ZTS php and load the extension via -d. PHP is already built with
# -fsanitize, so the runtime is linked into the php binary — every test child
# process runs under ASan/UBSan automatically.
#
# -n  : ignore system php.ini so the only extension loaded is ours.
# -d extension=... : load the akari .so (a normal extension, not zend_extension).
export NO_INTERACTION=1
export TEST_PHP_EXECUTABLE="$PHP_BIN"
export TEST_PHP_ARGS="-n -d extension=$EXT"

# Default to the whole suite; allow a subset via docker run args.
if [ "$#" -eq 0 ]; then
    set -- tests/
fi

exec php -n -d extension="$EXT" run-tests.php --show-diff "$@"
