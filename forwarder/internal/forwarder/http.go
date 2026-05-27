package forwarder

import (
	"bytes"
	"context"
	"fmt"
	"io"
	"net"
	"net/http"
	"time"
)

// HTTPForwarder sends OTLP JSON payloads to an HTTP endpoint.
type HTTPForwarder struct {
	endpoint string
	client   *http.Client
}

// NewHTTP creates a new HTTPForwarder targeting the given OTLP HTTP endpoint.
// The endpoint should be the base URL (e.g. "http://localhost:4318").
func NewHTTP(endpoint string) *HTTPForwarder {
	transport := &http.Transport{
		DialContext: (&net.Dialer{
			Timeout:   5 * time.Second,
			KeepAlive: 30 * time.Second,
		}).DialContext,
		MaxIdleConns:        10,
		MaxIdleConnsPerHost: 10,
		IdleConnTimeout:     90 * time.Second,
	}

	return &HTTPForwarder{
		endpoint: endpoint,
		client: &http.Client{
			Transport: transport,
			Timeout:   10 * time.Second,
		},
	}
}

// Forward sends the payload to the OTLP endpoint for the given signal
// (/v1/traces or /v1/logs).
func (f *HTTPForwarder) Forward(ctx context.Context, payload []byte, signal Signal) error {
	path := "/v1/traces"
	if signal == SignalLogs {
		path = "/v1/logs"
	}
	url := f.endpoint + path

	req, err := http.NewRequestWithContext(ctx, http.MethodPost, url, bytes.NewReader(payload))
	if err != nil {
		return fmt.Errorf("creating request: %w", err)
	}
	req.Header.Set("Content-Type", "application/json")

	resp, err := f.client.Do(req)
	if err != nil {
		return fmt.Errorf("sending request: %w", err)
	}
	defer resp.Body.Close()
	body, _ := io.ReadAll(resp.Body)

	if resp.StatusCode < 200 || resp.StatusCode >= 300 {
		return fmt.Errorf("OTLP endpoint returned status %d: %s", resp.StatusCode, string(body))
	}

	return nil
}

// Close is a no-op for the HTTP forwarder; idle connections are managed by the transport.
func (f *HTTPForwarder) Close() error {
	return nil
}
