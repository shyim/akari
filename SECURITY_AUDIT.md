# Akari Security Audit

**Audit date:** 2026-09-08

**Audited revision:** `eef1ed6db1a6b0822cdc21d83582182b20f3be0a`

**Scope:** Akari PHP profiler extension, UDP transport, Go forwarder, telemetry capture, collector communication, and related build automation.

## Executive summary

The audit identified seven actionable findings: three high severity, three medium severity, and one low severity. The most urgent issue is an unauthenticated UDP decoding path where a very small MessagePack packet can force a large allocation and terminate the forwarder. The UDP boundary also permits telemetry forgery and queue exhaustion.

At the audited revision, the profiler captured several categories of sensitive application data without central redaction. Full URLs, request URIs, command-line arguments, SQL statements, shell commands, cache keys, exception messages, and other values can reach the configured telemetry collector.

No exploitable native memory-corruption defect was identified in the reviewed paths. This audit is a source review with targeted reproductions; it is not a formal proof that the codebase is free from security defects.

## Remediation status (2026-09-08)

All seven findings have fixes implemented in this change. The descriptions and
source links below preserve the original audited revision; they describe the
pre-fix behavior, not the updated implementation. These changes have not yet
been published in a release.

| Finding | Status | Implemented change |
| --- | --- | --- |
| AKARI-SEC-001 | Fixed | Allocation-free MessagePack preflight validates the complete packet, collection sizes, depth, duplicate keys and field lengths before reflection decoding; decoded IDs and numeric ranges are checked. |
| AKARI-SEC-002 | Fixed | Network listeners require HMAC-SHA256 authentication. The protocol includes a timestamp, cryptographic nonce and bounded replay protection. Unsigned traffic is loopback-only. Receive rate is capped at 2,000 packets/second; queued backing buffers default to a 16 MiB limit. |
| AKARI-SEC-003 | Fixed by safer defaults | Shared native serialization policy omits raw statements, CLI arguments, `code.filepath` attributes and exception messages, and redacts log/context/tag values. System-only `akari.capture_sensitive=1` explicitly opts into raw text. URL credentials/query strings/fragments are stripped before truncation in either mode. SQL text is omitted rather than relying on the incomplete display normalizer. |
| AKARI-SEC-004 | Fixed | `createSpan()` rejects calls after 32 manual spans, matching shutdown tracking capacity. Every accepted manual span is finalized; the 100,000-call regression exports exactly 32 completed spans. |
| AKARI-SEC-005 | Fixed | Collector redirects are rejected for all redirect status codes; custom authentication headers stay with the configured collector. |
| AKARI-SEC-006 | Fixed | Collector response reads are capped at 64 KiB, and error bodies are quoted to escape control characters. |
| AKARI-SEC-007 | Fixed | Documentation actions and container bases are pinned to immutable revisions/digests. Python dependencies are version-locked with hashes. Documentation builds and privileged deployment run in separate jobs. |

Implementation and regression coverage:

- [MessagePack validation](forwarder/internal/transform/validate.go), [decoder regression tests and fuzz target](forwarder/internal/transform/security_test.go).
- [UDP authentication](forwarder/internal/receiver/auth.go), [authentication tests](forwarder/internal/receiver/auth_test.go), [native authenticated sender](src/udp_export.c).
- [Byte-bounded queue](forwarder/internal/buffer/buffer.go), [concurrency and budget tests](forwarder/internal/buffer/buffer_test.go).
- [Collector regression tests](forwarder/internal/forwarder/http_security_test.go).
- [Native privacy boundary](src/privacy.c), [manual-span regression](tests/280-manual-span-limit.phpt), [HMAC regression](tests/281-udp-authentication.phpt), [default privacy regression](tests/282-sensitive-capture-default.phpt), [URL truncation/redaction regression](tests/283-url-redaction-opt-in.phpt).
- [Transport setup](docs/reference/forwarder.md#transport-security), [capture policy](docs/getting-started/configuration.md#sensitive-data-capture).

### Validation of the fixes

- Go tests with the race detector and `go vet ./...` passed.
- Ten seconds of decoder fuzzing exercised approximately 490,000 inputs without a crash.
- PHP 8.4.7 ASan/UBSan: 130 passed, 17 skipped, zero failures. Skips cover unavailable integrations and Zend memory accounting disabled for sanitizers.
- PHP 8.2 ZTS: 139 passed, 8 skipped, zero failures. Skips cover unavailable Memcached, Redis/RedisCluster and MySQLi extensions.
- A live smoke test delivered PHP spans and redacted logs through authenticated UDP and the Go forwarder to a local HTTP collector.
- Documentation installed from the hash-locked dependency file and built successfully in an isolated Python 3.13 container.
- Docker Compose configuration validation passed with an explicitly supplied shared key.

### Deployment changes and remaining boundaries

Configure the same random `AKARI_UDP_KEY` (at least 32 bytes; generate with
`openssl rand -hex 32`) for PHP and the forwarder before using a network
listener. PHP also accepts system INI `akari.udp_key`. Ensure PHP-FPM workers
receive the key when their environment is cleared. The Compose demo now
requires the key explicitly.

HMAC authenticates packets but does not encrypt them. Use private networking
or an encrypted tunnel between hosts, and HTTPS for remote collectors. All
producers sharing a key are mutually trusted. Loopback without a key trusts
local processes. Replay protection uses a 30-second clock tolerance, is bounded,
fails closed if full, and resets when the receiver restarts.

`akari.capture_sensitive=1` deliberately restores raw application text and must
be chosen according to collector access and retention requirements. Even with
safe defaults, application-chosen span names, tag keys, URL paths and framework
route/template/destination labels must not contain secrets. Engine-generated
function names may also embed source locations.

The manual-span fix closes the reported allocation loop; native profiler memory
in general remains outside Zend's `memory_limit`. The request-start kill switch
is not a complete process memory limit. Use container/process limits to bound
overall resource usage. UDP is lossy and overload can still cause dropped
telemetry. Public release/deployment validation has not been performed for these
fixes.

## Original findings

### AKARI-SEC-001: MessagePack packet can exhaust forwarder memory

**Severity:** High; critical when the UDP listener is reachable from an untrusted network.

The UDP receiver accepts datagrams up to 65 KiB without authenticating or filtering their sender. The transform layer passes the datagram directly to `msgpack.Unmarshal`:

- [`forwarder/internal/receiver/udp.go`](https://github.com/shyim/akari/blob/eef1ed6db1a6b0822cdc21d83582182b20f3be0a/forwarder/internal/receiver/udp.go#L41)
- [`forwarder/internal/transform/otlp.go`](https://github.com/shyim/akari/blob/eef1ed6db1a6b0822cdc21d83582182b20f3be0a/forwarder/internal/transform/otlp.go#L306)
- [`forwarder/go.mod`](https://github.com/shyim/akari/blob/eef1ed6db1a6b0822cdc21d83582182b20f3be0a/forwarder/go.mod#L5)

The MessagePack decoder preallocates slices from attacker-controlled array lengths before confirming that the packet contains those elements. A 12-byte packet declaring an incomplete 100,000-element array caused 136,102,752 bytes of allocation during reproduction. The declared count can be increased to terminate the process with a single packet.

The default command-line listener uses loopback. The Docker Compose service binds the listener to `0.0.0.0` inside its container network, making it reachable by other workloads on that network.

**Recommendations:**

- Keep the receiver on a Unix datagram socket or loopback interface unless network access is explicitly required.
- Enforce host firewall and container network policy around UDP access.
- Validate MessagePack collection lengths and field sizes before allocating.
- Add protocol limits for spans, events, IDs, strings, and attributes.
- Add malformed-input fuzzing and a process-level memory limit.
- Do not rely on a dependency upgrade alone; the relevant preallocation path is present in the dependency's current upstream source.

### AKARI-SEC-002: UDP permits telemetry forgery and queue exhaustion

**Severity:** High.

The receiver discards the sender address and places opaque payloads into a fixed-count queue. The transform validates the protocol version but does not authenticate the message or strictly validate identifiers, counts, timestamps, service names, and span fields.

An attacker with access to the listener can therefore:

- Submit fabricated traces under the forwarder's configured collector credentials.
- Poison service names, trace data, and operational investigations.
- Consume collector quota and generate unexpected telemetry costs.
- Fill the queue while sequential HTTP forwarding is delayed.

The default queue contains 16,384 entries. With the maximum accepted UDP payload, it can retain approximately 1.09 GB of payload data, excluding Go allocation and channel overhead.

Relevant code:

- [`forwarder/internal/receiver/udp.go`](https://github.com/shyim/akari/blob/eef1ed6db1a6b0822cdc21d83582182b20f3be0a/forwarder/internal/receiver/udp.go#L13)
- [`forwarder/internal/transform/otlp.go`](https://github.com/shyim/akari/blob/eef1ed6db1a6b0822cdc21d83582182b20f3be0a/forwarder/internal/transform/otlp.go#L306)
- [`forwarder/cmd/akari-forwarder/main.go`](https://github.com/shyim/akari/blob/eef1ed6db1a6b0822cdc21d83582182b20f3be0a/forwarder/cmd/akari-forwarder/main.go#L20)

**Recommendations:**

- Authenticate producers or constrain the listener to a trusted local transport.
- Replace the count-only queue limit with a conservative byte budget.
- Add per-source rate limits and backpressure.
- Reject malformed identifiers, excessive collection sizes, invalid timestamps, and oversized strings.
- Batch collector requests so a slow request cannot stall the complete queue.

### AKARI-SEC-003: Sensitive application data is exported without redaction

**Severity:** High.

Instrumentation copies application-controlled values into spans and logs without a central sanitization policy. Captured values include:

- Full cURL and stream URLs, including query strings.
- HTTP request URIs used as `url.path` and span names.
- Full CLI argument lists.
- Shell commands, email recipients, and filesystem paths.
- Raw SQL statements and database errors.
- Redis, Memcached, and APCu keys.
- GraphQL query text.
- Exception messages, log bodies, and structured log context.

Relevant code:

- [`src/hook_curl.c`](https://github.com/shyim/akari/blob/eef1ed6db1a6b0822cdc21d83582182b20f3be0a/src/hook_curl.c#L363)
- [`src/hook_root_span.c`](https://github.com/shyim/akari/blob/eef1ed6db1a6b0822cdc21d83582182b20f3be0a/src/hook_root_span.c#L125)
- [`src/hook_io.c`](https://github.com/shyim/akari/blob/eef1ed6db1a6b0822cdc21d83582182b20f3be0a/src/hook_io.c#L243)
- [`src/sql_normalize.c`](https://github.com/shyim/akari/blob/eef1ed6db1a6b0822cdc21d83582182b20f3be0a/src/sql_normalize.c#L1)

The repository contains a SQL normalization implementation, but the reviewed instrumentation paths do not call it. As a result, signed URL credentials, API tokens, passwords, personal information, session identifiers, and SQL literals may enter telemetry storage. Data is first transmitted over an unauthenticated plaintext UDP channel.

OpenTelemetry's URL conventions require URL credentials to be excluded and recommend scrubbing sensitive query parameters:

- [OpenTelemetry URL semantic conventions](https://opentelemetry.io/docs/specs/semconv/url/)
- [OpenTelemetry HTTP span conventions](https://opentelemetry.io/docs/specs/semconv/http/http-spans/)

**Recommendations:**

- Introduce central sanitization before values enter spans or logs.
- Remove URL user information and redact configured query parameters.
- Parse the request path separately from the query string and use low-cardinality route names.
- Apply SQL literal normalization before export.
- Redact command-line flags that commonly contain credentials.
- Add allowlists and configuration switches for sensitive capture categories.
- Document the remaining data-capture behavior clearly for operators.

### AKARI-SEC-004: Manual spans bypass PHP memory limits

**Severity:** Medium.

`Akari\createSpan()` permits spans until the profiler's global capacity is reached. Native storage is allocated with libc allocation functions and is not included in Zend's `memory_limit` accounting. Only 32 open manual spans are retained for shutdown finalization, while the profiler allows up to 262,144 total spans.

Relevant code:

- [`src/php_akari.c`](https://github.com/shyim/akari/blob/eef1ed6db1a6b0822cdc21d83582182b20f3be0a/src/php_akari.c#L414)
- [`src/profiler.h`](https://github.com/shyim/akari/blob/eef1ed6db1a6b0822cdc21d83582182b20f3be0a/src/profiler.h#L12)
- [`src/profiler.h`](https://github.com/shyim/akari/blob/eef1ed6db1a6b0822cdc21d83582182b20f3be0a/src/profiler.h#L440)
- [`src/profiler.c`](https://github.com/shyim/akari/blob/eef1ed6db1a6b0822cdc21d83582182b20f3be0a/src/profiler.c#L220)
- [`src/profiler_span.c`](https://github.com/shyim/akari/blob/eef1ed6db1a6b0822cdc21d83582182b20f3be0a/src/profiler_span.c#L730)

During reproduction, 262,145 calls were made with PHP configured with `memory_limit=16M`. Zend reported a peak of approximately 2 MB while the process reached 124,576 KB maximum RSS. The fixed native span array can consume approximately 107 MB by itself.

This weakens `memory_limit` as an isolation boundary for applications that execute third-party plugins, templates, or tenant-controlled PHP code.

**Recommendations:**

- Apply a small explicit limit to open and total manual spans per request.
- Track and limit native allocation growth.
- Use Zend-managed allocation where practical so memory is accounted consistently.
- Finalize or reject every accepted manual span instead of retaining only the first 32.
- Include native profiler usage in the memory kill switch.

### AKARI-SEC-005: Collector credentials are forwarded across redirects

**Severity:** Medium.

The forwarder's HTTP client follows redirects using Go's default policy. Custom collector authentication headers are attached to the initial request and remain present on cross-host redirects. Go automatically strips only a defined set of sensitive headers; custom headers such as `X-Honeycomb-Team` are outside that set.

Relevant code and documentation:

- [`forwarder/internal/forwarder/http.go`](https://github.com/shyim/akari/blob/eef1ed6db1a6b0822cdc21d83582182b20f3be0a/forwarder/internal/forwarder/http.go#L24)
- [`docs/reference/forwarder.md`](https://github.com/shyim/akari/blob/eef1ed6db1a6b0822cdc21d83582182b20f3be0a/docs/reference/forwarder.md#L51)
- [Go `net/http` documentation](https://pkg.go.dev/net/http)

An isolated reproduction used a `307` response to redirect the forwarder to a different mock host. The redirect target received the configured `X-Honeycomb-Team: secret-api-key` header.

Exploitation requires control of the configured collector or the ability to intercept and modify plaintext HTTP traffic.

**Recommendations:**

- Disable redirects for collector requests, or allow only same-origin HTTPS redirects.
- Apply authentication headers only after validating the destination authority.
- Require HTTPS collector endpoints outside explicitly local environments.

### AKARI-SEC-006: Collector error responses are read without a size limit

**Severity:** Medium.

The forwarder calls `io.ReadAll` on every collector response before checking its status. A compromised collector or an attacker able to intercept plaintext HTTP can return a large response body, causing avoidable memory growth. The body is included in an error and can contain newlines or control characters that alter log output.

Relevant code:

- [`forwarder/internal/forwarder/http.go`](https://github.com/shyim/akari/blob/eef1ed6db1a6b0822cdc21d83582182b20f3be0a/forwarder/internal/forwarder/http.go#L63)

The client's ten-second timeout limits the response duration but does not provide a useful byte limit for a fast response.

**Recommendations:**

- Read error bodies through `io.LimitReader`, with a limit such as 64 KiB.
- Discard successful response bodies after ensuring connection reuse requirements are met.
- Sanitize control characters and truncate bodies before including them in logs.

### AKARI-SEC-007: Build automation uses mutable dependencies

**Severity:** Low.

The documentation workflow uses mutable major action tags and installs an unpinned Python package while holding `pages: write` and `id-token: write` permissions. Container definitions also use mutable base-image tags, including `latest`.

Relevant files:

- [`.github/workflows/docs.yml`](https://github.com/shyim/akari/blob/eef1ed6db1a6b0822cdc21d83582182b20f3be0a/.github/workflows/docs.yml#L18)
- [`forwarder/Dockerfile`](https://github.com/shyim/akari/blob/eef1ed6db1a6b0822cdc21d83582182b20f3be0a/forwarder/Dockerfile#L1)
- [`compose.yml`](https://github.com/shyim/akari/blob/eef1ed6db1a6b0822cdc21d83582182b20f3be0a/compose.yml#L1)

Compromise or unexpected replacement of one of these dependencies could affect published documentation or produced container images.

**Recommendations:**

- Pin GitHub Actions to reviewed commit SHAs.
- Pin Python dependencies to reviewed versions with hashes.
- Pin container images by immutable digest.
- Keep workflow permissions scoped to the individual step or job that requires them.

## Original remediation priorities

1. Restrict UDP reachability and implement bounded decoding for AKARI-SEC-001.
2. Add source authentication, byte-bounded buffering, and protocol validation for AKARI-SEC-002.
3. Establish a central telemetry redaction policy for AKARI-SEC-003.
4. Bound native manual-span memory for AKARI-SEC-004.
5. Harden collector redirects and response handling for AKARI-SEC-005 and AKARI-SEC-006.
6. Pin build and workflow dependencies for AKARI-SEC-007.

## Original audit validation

- `GOCACHE=/tmp/akari-go-cache go test ./...` passed.
- `GOCACHE=/tmp/akari-go-cache go test ./... -race` passed.
- A malformed MessagePack allocation reproducer confirmed 136,102,752 bytes of allocation from a 12-byte packet.
- An isolated redirect test confirmed that a custom collector API key reaches a cross-host redirect target.
- A production extension build reproduced native memory growth beyond PHP's configured `memory_limit`.
- The current public CI run passed PHP 8.2 through 8.5, ASan/UBSan, optional integrations, and Go forwarder tests: [GitHub Actions run 34089146750](https://github.com/shyim/akari/actions/runs/34089146750).

A fresh local PHPT run was unavailable because the checkout's generated Makefile contains stale `/work` source paths and the installed production module does not expose debug introspection functions required by some tests. The current public CI run provides the native multi-version test evidence cited above.
