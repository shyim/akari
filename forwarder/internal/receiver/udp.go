package receiver

import (
	"context"
	"log"
	"net"
	"os"
	"sync/atomic"
	"time"

	"github.com/shyim/akari-forwarder/internal/buffer"
	"github.com/shyim/akari-forwarder/internal/transform"
)

const (
	// maxDatagramSize is the maximum UDP datagram size we accept (65 KB).
	maxDatagramSize = 65 * 1024
	// recvBufSize is the SO_RCVBUF size to request from the OS (4 MB).
	recvBufSize = 4 * 1024 * 1024
	// readDeadline is the read timeout used for clean shutdown checks.
	readDeadline = 1 * time.Second
)

// UDPReceiver listens for UDP datagrams and enqueues them into a Buffer.
type UDPReceiver struct {
	addr string
	buf  *buffer.Buffer

	// Stats counters — use atomic loads/stores.
	PacketsReceived atomic.Uint64
	BytesReceived   atomic.Uint64
	PacketsDropped  atomic.Uint64
}

// New creates a new UDPReceiver bound to the given address, writing into buf.
func New(addr string, buf *buffer.Buffer) *UDPReceiver {
	return &UDPReceiver{
		addr: addr,
		buf:  buf,
	}
}

// Run binds a UDP socket and reads datagrams in a loop until ctx is cancelled.
func (r *UDPReceiver) Run(ctx context.Context) error {
	key := os.Getenv("AKARI_UDP_KEY")
	addr, err := resolveListen(r.addr, key)
	if err != nil {
		return err
	}
	pc, err := net.ListenUDP("udp", addr)
	if err != nil {
		return err
	}
	defer pc.Close()

	// Try to increase the OS receive buffer.
	if err := pc.SetReadBuffer(recvBufSize); err != nil {
		log.Printf("warning: failed to set SO_RCVBUF to %d: %v", recvBufSize, err)
	}

	log.Printf("UDP receiver listening on %s", r.addr)

	readBuf := make([]byte, maxDatagramSize)
	auth := authenticator{key: []byte(key)}
	window, packets := time.Now(), 0

	for {
		// Check for cancellation.
		select {
		case <-ctx.Done():
			log.Println("UDP receiver shutting down")
			return nil
		default:
		}

		// Set a read deadline so we periodically re-check ctx.
		if err := pc.SetReadDeadline(time.Now().Add(readDeadline)); err != nil {
			return err
		}

		n, source, err := pc.ReadFromUDP(readBuf)
		if err != nil {
			if netErr, ok := err.(net.Error); ok && netErr.Timeout() {
				continue // deadline expired, loop back to check ctx
			}
			return err
		}

		r.PacketsReceived.Add(1)
		r.BytesReceived.Add(uint64(n))
		now := time.Now()
		if now.Sub(window) >= time.Second {
			window, packets = now, 0
		}
		packets++
		if packets > 2000 || (key == "" && !source.IP.IsLoopback()) {
			r.PacketsDropped.Add(1)
			continue
		}
		data, ok := auth.unwrap(readBuf[:n], now)
		if !ok || transform.ValidateWire(data) != nil {
			r.PacketsDropped.Add(1)
			continue
		}

		// Copy the data so the read buffer can be reused.
		payload := make([]byte, len(data))
		copy(payload, data)

		if !r.buf.TryEnqueue(payload) {
			r.PacketsDropped.Add(1)
			if r.PacketsDropped.Load()%1000 == 1 {
				log.Printf("buffer full — dropped packet (%d total dropped)", r.PacketsDropped.Load())
			}
		}
	}
}
