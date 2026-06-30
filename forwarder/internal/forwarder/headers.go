package forwarder

import (
	"fmt"
	"net/url"
	"strings"
)

// ParseHeaders parses the value of OTEL_EXPORTER_OTLP_HEADERS into a map.
//
// Per the OpenTelemetry specification the value is a comma-separated list of
// key=value pairs, matching the W3C Baggage format. Keys and values may be
// surrounded by whitespace (which is trimmed) and values are percent-decoded.
//
//	api-key=secret,x-tenant=acme%20corp
func ParseHeaders(raw string) (map[string]string, error) {
	raw = strings.TrimSpace(raw)
	if raw == "" {
		return nil, nil
	}

	headers := make(map[string]string)
	for _, pair := range strings.Split(raw, ",") {
		pair = strings.TrimSpace(pair)
		if pair == "" {
			continue
		}

		key, value, ok := strings.Cut(pair, "=")
		if !ok {
			return nil, fmt.Errorf("invalid header %q: expected key=value", pair)
		}

		key = strings.TrimSpace(key)
		if key == "" {
			return nil, fmt.Errorf("invalid header %q: empty key", pair)
		}

		value = strings.TrimSpace(value)
		decoded, err := url.PathUnescape(value)
		if err != nil {
			return nil, fmt.Errorf("invalid header value for %q: %w", key, err)
		}

		headers[key] = decoded
	}

	return headers, nil
}
