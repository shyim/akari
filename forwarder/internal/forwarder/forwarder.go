package forwarder

import "context"

// Signal identifies which OTLP signal a payload belongs to, selecting the
// destination endpoint (/v1/traces vs /v1/logs).
type Signal int

const (
	SignalTraces Signal = iota
	SignalLogs
)

// Forwarder sends OTLP payloads to a backend collector.
type Forwarder interface {
	Forward(ctx context.Context, payload []byte, signal Signal) error
	Close() error
}
