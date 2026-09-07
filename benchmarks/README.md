# Benchmarks

The [CodSpeed workflow](../.github/workflows/codspeed.yml) measures the PHP
extension and Go forwarder on pushes to `main`, pull requests, and manual runs.
Both jobs use GitHub-hosted `ubuntu-latest` runners and OIDC authentication;
no custom runners or `CODSPEED_TOKEN` secret are needed. Connect `shyim/akari`
in CodSpeed to receive reports. See
[CodSpeed's GitHub setup](https://codspeed.io/docs/integrations/ci/github-actions/configuration).

Walltime measurements on shared GitHub runners vary with host load and hardware.
Use results to spot trends and larger regressions, and confirm small changes with
repeated runs on consistent hardware. CodSpeed recommends dedicated Macro Runners
for precise walltime comparisons, but those require a GitHub organization.

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
`--skip-upload`. Local and GitHub-hosted walltime results depend on the machine
and its load; account for that variance when comparing commits.

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
