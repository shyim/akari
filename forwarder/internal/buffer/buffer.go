package buffer

import "time"

// Buffer is a bounded channel-based payload buffer for incoming UDP datagrams.
type Buffer struct {
	ch chan []byte
}

// New creates a new Buffer with the given maximum capacity.
func New(maxSize int) *Buffer {
	return &Buffer{
		ch: make(chan []byte, maxSize),
	}
}

// TryEnqueue attempts a non-blocking send of payload into the buffer.
// Returns false if the buffer is full (the payload is dropped).
func (b *Buffer) TryEnqueue(payload []byte) bool {
	select {
	case b.ch <- payload:
		return true
	default:
		return false
	}
}

// DequeueBatch waits for the first item (or until timeout expires), then
// drains up to maxBatch items from the buffer in a non-blocking fashion.
func (b *Buffer) DequeueBatch(maxBatch int, timeout time.Duration) [][]byte {
	batch := make([][]byte, 0, maxBatch)

	// Wait for the first item or timeout.
	select {
	case item := <-b.ch:
		batch = append(batch, item)
	case <-time.After(timeout):
		return batch
	}

	// Drain up to maxBatch-1 more items non-blocking.
	for len(batch) < maxBatch {
		select {
		case item := <-b.ch:
			batch = append(batch, item)
		default:
			return batch
		}
	}

	return batch
}

// Drain returns all remaining items from the buffer without blocking.
func (b *Buffer) Drain() [][]byte {
	var items [][]byte
	for {
		select {
		case item := <-b.ch:
			items = append(items, item)
		default:
			return items
		}
	}
}
