---
icon: lucide/wrench
---

# Installation

Akari has two components: the **PHP extension** (`akari.so`), which runs inside
PHP-FPM/CLI and captures spans, and the **Go forwarder**, which receives those
spans over UDP and forwards them to your OTLP collector.

## Prerequisites

- PHP 8.2+ with development headers (`phpize`)
- A C toolchain (`gcc`/`clang`, `make`)
- Go 1.26+ (only needed to build the forwarder from source)
- macOS or Linux

## 1. Build the extension

```bash
phpize
./configure --enable-akari
make -j$(nproc)
make install
```

!!! note "Debug builds"

    Add `--enable-akari-debug` to compile in the
    [debug introspection functions](../reference/php-api.md#debug-introspection-debug-builds-only)
    (`getSpansJson`, etc.). These are for testing/debugging only — leave the
    flag off for production builds.

## 2. Build the forwarder

```bash
cd forwarder
go build -o akari-forwarder ./cmd/akari-forwarder
```

Alternatively, skip this step and use the pre-built container image — see
[Quick start](quick-start.md#1-run-the-forwarder-docker).

## 3. Configure PHP

Enable the extension in your `php.ini`:

```ini
extension=akari.so
akari.enable=1
akari.service_name=my-app
```

See [Configuration](configuration.md) for every available INI setting.

## 4. Start the forwarder

```bash
OTEL_EXPORTER_OTLP_ENDPOINT=http://localhost:4318 ./akari-forwarder
```

Then restart PHP-FPM or run your CLI app. Traces will appear in your collector
within seconds.

## Verify the install

Confirm the extension is loaded:

```bash
php -m | grep akari
php --ri akari
```

`php --ri akari` prints the active INI values so you can confirm tracing is
enabled and pointed at the right forwarder host/port.
