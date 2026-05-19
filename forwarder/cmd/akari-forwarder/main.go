package main

import (
	"context"
	"log"
	"os"
	"os/signal"
	"strconv"
	"syscall"
	"time"

	"github.com/shyim/akari-forwarder/internal/buffer"
	"github.com/shyim/akari-forwarder/internal/forwarder"
	"github.com/shyim/akari-forwarder/internal/receiver"
	"github.com/shyim/akari-forwarder/internal/transform"
)

func main() {
	// Configuration from environment variables with defaults.
	listenAddr := envOrDefault("OTEL_FORWARDER_LISTEN", "127.0.0.1:4319")
	otlpEndpoint := envOrDefault("OTEL_EXPORTER_OTLP_ENDPOINT", "http://localhost:4318")
	bufferSize := envOrDefaultInt("OTEL_FORWARDER_BUFFER_SIZE", 16384)
	batchSize := envOrDefaultInt("OTEL_FORWARDER_BATCH_SIZE", 64)
	flushInterval := envOrDefaultDuration("OTEL_FORWARDER_FLUSH_INTERVAL", 100*time.Millisecond)

	log.Printf("akari-forwarder starting")
	log.Printf("  listen:   %s (UDP)", listenAddr)
	log.Printf("  endpoint: %s (HTTP/JSON)", otlpEndpoint)
	log.Printf("  buffer:   %d slots", bufferSize)
	log.Printf("  batch:    %d max, flush every %s", batchSize, flushInterval)

	// Create components.
	buf := buffer.New(bufferSize)
	recv := receiver.New(listenAddr, buf)
	fwd := forwarder.NewHTTP(otlpEndpoint)

	// Set up signal-based shutdown.
	ctx, stop := signal.NotifyContext(context.Background(), syscall.SIGINT, syscall.SIGTERM)
	defer stop()

	// Start the UDP receiver in a goroutine.
	go func() {
		if err := recv.Run(ctx); err != nil {
			log.Fatalf("UDP receiver error: %v", err)
		}
	}()

	// Flush loop: dequeue batches and forward them.
	flushLoop(ctx, buf, fwd, batchSize, flushInterval)

	// Shutdown: drain remaining buffered payloads.
	log.Println("shutting down — draining buffer")
	remaining := buf.Drain()
	if len(remaining) > 0 {
		log.Printf("draining %d remaining payloads", len(remaining))
		drainCtx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
		defer cancel()
		forwardBatch(drainCtx, remaining, fwd)
	}

	if err := fwd.Close(); err != nil {
		log.Printf("error closing forwarder: %v", err)
	}

	log.Printf("stats: packets_received=%d bytes_received=%d packets_dropped=%d",
		recv.PacketsReceived.Load(),
		recv.BytesReceived.Load(),
		recv.PacketsDropped.Load(),
	)
	log.Println("akari-forwarder stopped")
}

func flushLoop(ctx context.Context, buf *buffer.Buffer, fwd forwarder.Forwarder, batchSize int, flushInterval time.Duration) {
	for {
		select {
		case <-ctx.Done():
			return
		default:
		}

		batch := buf.DequeueBatch(batchSize, flushInterval)
		if len(batch) == 0 {
			continue
		}

		forwardBatch(ctx, batch, fwd)
	}
}

func forwardBatch(ctx context.Context, batch [][]byte, fwd forwarder.Forwarder) {
	for _, payload := range batch {
		otlpData, err := transform.Transform(payload)
		if err != nil {
			log.Printf("transform error (%d bytes): %v", len(payload), err)
			continue
		}

		if err := fwd.Forward(ctx, otlpData); err != nil {
			log.Printf("forward error: %v", err)
		}
	}
}

func envOrDefault(key, defaultVal string) string {
	if v := os.Getenv(key); v != "" {
		return v
	}
	return defaultVal
}

func envOrDefaultInt(key string, defaultVal int) int {
	if v := os.Getenv(key); v != "" {
		n, err := strconv.Atoi(v)
		if err != nil {
			log.Printf("warning: invalid integer for %s=%q, using default %d", key, v, defaultVal)
			return defaultVal
		}
		return n
	}
	return defaultVal
}

func envOrDefaultDuration(key string, defaultVal time.Duration) time.Duration {
	if v := os.Getenv(key); v != "" {
		d, err := time.ParseDuration(v)
		if err != nil {
			log.Printf("warning: invalid duration for %s=%q, using default %s", key, v, defaultVal)
			return defaultVal
		}
		return d
	}
	return defaultVal
}
