package receiver

import (
	"crypto/hmac"
	"crypto/sha256"
	"encoding/binary"
	"errors"
	"net"
	"time"
)

const envelopeSize = 60 // AKR1 + seconds (8) + random nonce (16) + HMAC (32)

// resolveListen forbids unauthenticated network listeners, including wildcard
// and hostnames resolving outside loopback. Bind the resolved address to avoid
// a second DNS resolution changing the address after validation.
func resolveListen(addr, key string) (*net.UDPAddr, error) {
	a, err := net.ResolveUDPAddr("udp", addr)
	if err != nil {
		return nil, err
	}
	if key != "" && len(key) < 32 {
		return nil, errors.New("AKARI_UDP_KEY must contain at least 32 bytes")
	}
	if key == "" && !a.IP.IsLoopback() {
		return nil, errors.New("non-loopback UDP requires AKARI_UDP_KEY")
	}
	return a, nil
}

type authenticator struct {
	key       []byte
	seen      map[[32]byte]int64
	lastSweep int64
}

func (a *authenticator) unwrap(data []byte, now time.Time) ([]byte, bool) {
	if len(a.key) == 0 {
		return data, true
	}
	if len(data) <= envelopeSize || string(data[:4]) != "AKR1" {
		return nil, false
	}
	seconds := binary.BigEndian.Uint64(data[4:12])
	current := now.Unix()
	if seconds > uint64(current+30) || seconds < uint64(current-30) {
		return nil, false
	}
	mac := hmac.New(sha256.New, a.key)
	mac.Write(data[:28])
	mac.Write(data[60:])
	if !hmac.Equal(data[28:60], mac.Sum(nil)) {
		return nil, false
	}
	if a.seen == nil {
		a.seen = make(map[[32]byte]int64)
	}
	if a.lastSweep != current {
		for k, expiry := range a.seen {
			if expiry < current {
				delete(a.seen, k)
			}
		}
		a.lastSweep = current
	}
	var tag [32]byte
	copy(tag[:], data[28:60])
	if _, exists := a.seen[tag]; exists || len(a.seen) >= 65536 {
		return nil, false
	}
	a.seen[tag] = int64(seconds) + 30
	return data[60:], true
}
