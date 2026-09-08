package buffer

import (
	"sync"
	"testing"
	"time"
)

func TestByteBudget(t *testing.T) {
	b := NewWithByteLimit(10, 100)
	if !b.TryEnqueue(make([]byte, 60)) || b.TryEnqueue(make([]byte, 60)) {
		t.Fatal("byte limit not enforced")
	}
	if b.TryEnqueue(make([]byte, 1, 101)) {
		t.Fatal("backing capacity not accounted")
	}
	b.DequeueBatch(1, time.Second)
	if !b.TryEnqueue(make([]byte, 100)) {
		t.Fatal("dequeue did not release budget")
	}
	b.Drain()
	if b.bytes.Load() != 0 {
		t.Fatal("drain did not release budget")
	}
}

func TestConcurrentBudget(t *testing.T) {
	b := NewWithByteLimit(100, 100)
	var wg sync.WaitGroup
	for i := 0; i < 100; i++ {
		wg.Add(1)
		go func() { defer wg.Done(); b.TryEnqueue(make([]byte, 10)) }()
	}
	wg.Wait()
	if n := len(b.Drain()); n != 10 {
		t.Fatalf("accepted %d payloads", n)
	}
	if b.bytes.Load() != 0 {
		t.Fatal("budget leaked")
	}
}
