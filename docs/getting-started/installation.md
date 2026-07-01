---
icon: lucide/wrench
---

# Installation

Akari has two components: the **PHP extension** (`akari.so`), which runs inside
PHP-FPM/CLI and captures spans, and the **Go forwarder**, which receives those
spans over UDP and forwards them to your OTLP collector.

## 1. Install the extension

### With PIE (recommended)

[PIE][pie] (the PHP Installer for Extensions) is the simplest way to install
Akari. It downloads, builds, and installs the extension in one step:

```bash
pie install shyim/akari
```

PIE compiles the extension against your active PHP and installs `akari.so` into
the correct extension directory. You still need to enable it — see
[Configure PHP](#2-configure-php) below.

  [pie]: https://github.com/php/pie

### From source

If you prefer to build manually (or want a
[debug build](#debug-builds)), clone the repository and use the standard
`phpize` flow.

**Prerequisites**

- PHP 8.2+ with development headers (`phpize`)
- A C toolchain (`gcc`/`clang`, `make`)
- macOS or Linux

```bash
phpize
./configure --enable-akari
make -j$(nproc)
make install
```

#### Debug builds

Add `--enable-akari-debug` to compile in the
[debug introspection functions](../reference/php-api.md#debug-introspection-debug-builds-only)
(`getSpansJson`, etc.). These are for testing/debugging only — leave the flag
off for production builds.

## 2. Configure PHP

Enable the extension in your `php.ini`:

```ini
extension=akari.so
akari.enable=1
akari.service_name=my-app
```

See [Configuration](configuration.md) for every available INI setting.

## 3. Run the forwarder

The forwarder is published as a container image on GHCR — the fastest way to
get it running:

```bash
docker run -d --name akari-forwarder \
  -p 4319:4319/udp \
  -e OTEL_EXPORTER_OTLP_ENDPOINT=http://localhost:4318 \
  ghcr.io/shyim/akari/akari-forwarder:latest
```

Prefer to build it yourself? It's a standard Go binary (requires Go 1.26+):

```bash
cd forwarder
go build -o akari-forwarder ./cmd/akari-forwarder
OTEL_EXPORTER_OTLP_ENDPOINT=http://localhost:4318 ./akari-forwarder
```

See [Forwarder](../reference/forwarder.md) for every configuration option.

## 4. Verify the install

Restart PHP-FPM or run your CLI app — traces will appear in your collector
within seconds. Confirm the extension is loaded:

```bash
php -m | grep akari
php --ri akari
```

`php --ri akari` prints the active INI values so you can confirm tracing is
enabled and pointed at the right forwarder host/port.
