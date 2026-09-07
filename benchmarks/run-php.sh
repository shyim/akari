#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "${BASH_SOURCE[0]}")/.."
scenario="${1:?Usage: bash benchmarks/run-php.sh <scenario>}"
enabled=1
sampler=always_on
workload="$scenario"
case "$scenario" in
    calls-disabled) enabled=0; workload=calls ;;
    calls-unsampled) sampler=always_off; workload=calls ;;
    calls-enabled) workload=calls ;;
    spans|logs|sqlite) ;;
    *) echo "Unknown benchmark: $scenario" >&2; exit 1 ;;
esac

# Ignore ambient PHP configuration and sampling/parent context. Port 9 is a
# loopback discard destination; no collector is needed or included in the run.
unset TRACEPARENT OTEL_TRACES_SAMPLER OTEL_TRACES_SAMPLER_ARG
extra_extensions=()
if [[ "$workload" == sqlite ]] && ! "${PHP_BIN:-php}" -n -r 'exit(class_exists("SQLite3") ? 0 : 1);'; then
    extra_extensions=(-d extension=sqlite3)
fi
exec "${PHP_BIN:-php}" -n \
    "${extra_extensions[@]}" \
    -d "extension=${AKARI_EXTENSION:-$PWD/modules/akari.so}" \
    -d "akari.enable=$enabled" \
    -d "akari.traces_sampler=$sampler" \
    -d akari.service_name=akari-benchmark \
    -d akari.trace_cli=1 \
    -d akari.trace_compile=0 \
    -d akari.trace_gc=0 \
    -d akari.min_duration_ms=0 \
    -d akari.flush_threshold=4096 \
    -d akari.udp_host=127.0.0.1 \
    -d akari.udp_port=9 \
    benchmarks/php.php "$workload"
