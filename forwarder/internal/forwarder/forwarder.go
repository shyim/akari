package forwarder

import "context"

// Forwarder sends trace payloads to a backend collector.
type Forwarder interface {
	Forward(ctx context.Context, payload []byte) error
	Close() error
}
