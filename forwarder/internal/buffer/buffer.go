package buffer

import (
	"sync/atomic"
	"time"
)

// Buffer is a bounded channel-based payload buffer for incoming UDP datagrams.
type Buffer struct {
	ch       chan []byte
	bytes    atomic.Int64
	maxBytes int64
}

// New creates a new Buffer with the given maximum capacity.
func New(maxSize int) *Buffer {
	return NewWithByteLimit(maxSize, 16*1024*1024)
}

func NewWithByteLimit(maxSize int, maxBytes int64) *Buffer {
	if maxSize <= 0 || maxSize > 65536 || maxBytes <= 0 || maxBytes > 256*1024*1024 {
		panic("invalid buffer limits")
	}
	return &Buffer{
		ch:       make(chan []byte, maxSize),
		maxBytes: maxBytes,
	}
}

// TryEnqueue attempts a non-blocking send of payload into the buffer.
// Returns false if the buffer is full (the payload is dropped).
func (b *Buffer) TryEnqueue(payload []byte) bool {
	n := int64(cap(payload))
	for {
		current := b.bytes.Load()
		if n > b.maxBytes-current {
			return false
		}
		if b.bytes.CompareAndSwap(current, current+n) {
			break
		}
	}
	select {
	case b.ch <- payload:
		return true
	default:
		b.bytes.Add(-n)
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
		b.bytes.Add(-int64(cap(item)))
		batch = append(batch, item)
	case <-time.After(timeout):
		return batch
	}

	// Drain up to maxBatch-1 more items non-blocking.
	for len(batch) < maxBatch {
		select {
		case item := <-b.ch:
			b.bytes.Add(-int64(cap(item)))
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
			b.bytes.Add(-int64(cap(item)))
			items = append(items, item)
		default:
			return items
		}
	}
}
