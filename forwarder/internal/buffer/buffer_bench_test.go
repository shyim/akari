package buffer

import (
	"fmt"
	"testing"
	"time"
)

func BenchmarkBufferBatch(b *testing.B) {
	for _, count := range []int{1, 16, 64} {
		b.Run(fmt.Sprintf("payloads=%d", count), func(b *testing.B) {
			queue := New(16384)
			payload := make([]byte, 1024)
			b.ReportAllocs()
			b.SetBytes(int64(count * len(payload)))
			for b.Loop() {
				// Measure enqueue and dequeue together, with a populated queue
				// so the benchmark never waits for the timeout.
				for range count {
					if !queue.TryEnqueue(payload) {
						b.Fatal("unexpected dropped payload")
					}
				}
				batch := queue.DequeueBatch(count, time.Hour)
				if len(batch) != count {
					b.Fatal("incomplete batch")
				}
			}
		})
	}
}

func BenchmarkBufferFull(b *testing.B) {
	queue := New(64)
	payload := make([]byte, 1024)
	for range 64 {
		queue.TryEnqueue(payload)
	}
	b.ReportAllocs()
	for b.Loop() {
		if queue.TryEnqueue(payload) {
			b.Fatal("full queue accepted a payload")
		}
	}
}
