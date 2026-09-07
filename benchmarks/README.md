# Benchmarks

The [CodSpeed workflow](../.github/workflows/codspeed.yml) measures the PHP
extension and Go forwarder on pushes to `main`, pull requests, and manual runs.
Connect `shyim/akari` in CodSpeed and enable its **Macro Runners** for the repository
before running the workflow. Both jobs use `codspeed-macro` for consistent walltime
measurements and OIDC authentication; no `CODSPEED_TOKEN` secret is needed.
See [CodSpeed's GitHub setup](https://codspeed.io/docs/integrations/ci/github-actions/configuration)
and [Macro Runner setup](https://codspeed.io/docs/features/macro-runners).

## PHP extension

Build with PHP 8.5 development tools and libcurl development headers:

```bash
phpize
./configure --enable-akari CFLAGS="-O2 -g"
make -j$(nproc)
bash benchmarks/run-php.sh spans
```

Use a production build without `--enable-akari-debug` or sanitizers. The runner
ignores ambient PHP INI files, OPcache/JIT, and inherited sampling configuration.
SQLite3 must be built into PHP or available as `sqlite3.so`. Set `PHP_BIN` and
`AKARI_EXTENSION` to select a different executable or extension build.

| Benchmark | Work per command |
| --- | --- |
| `php/calls-disabled` | 1,000,000 uninstrumented function calls with Akari loaded but disabled |
| `php/calls-unsampled` | Same calls with the `always_off` sampler |
| `php/calls-enabled` | Same calls with tracing enabled |
| `php/spans` | 20,000 attribute-instrumented calls, repeated frame lookup and span flushing |
| `php/logs` | 10,000 structured log records, including serialization and flushing |
| `php/sqlite` | In-memory database setup and 5,000 traced SQL queries |

The [CLI harness](https://codspeed.io/docs/benchmarks/cli-commands) measures each
whole command: shell/PHP startup, fixture setup, the workload, export, and cleanup.
These are request workload measurements, not per-function timings. The SQLite
launcher also probes whether SQLite3 is built in before loading a shared module.
Each workload checks its result and fails if prerequisites are missing.
Exports go to loopback UDP port 9 and are discarded; no forwarder or collector
should listen there. Serialization and UDP send attempts are included, but delivery
and collector throughput are not measured.

Run all six through CodSpeed:

```bash
# Install the CLI once: https://codspeed.io/docs/cli
codspeed run -m walltime --skip-upload
```

For uploaded local results, first run `codspeed auth login`, then omit
`--skip-upload`. Local walltime results depend on the machine and its load;
use the CI reports to compare commits.

## Go forwarder

```bash
cd forwarder
go test -run '^$' -bench . -benchmem ./...
codspeed run -m walltime --skip-upload -- go test -bench=. ./...
```

The native Go benchmarks cover MessagePack decoding through OTLP JSON encoding
for traces and logs at 1, 16, and 64 records per datagram. Fixtures use fixed
timestamps, realistic attributes, and the extension's ASCII hex identifiers.
Fixture construction and result validation are outside the timed `b.Loop()`.
Buffer benchmarks measure enqueue/dequeue batches of 1, 16, and 64 payloads,
plus rejection when the queue is full. They need no network services.
`go test` reports allocations as well as time per operation.

Keep the CodSpeed Go invocation to its supported `-bench` flag; use ordinary
`go test` for additional options such as `-benchmem` and `-benchtime`.
