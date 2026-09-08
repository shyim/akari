package receiver

import (
	"crypto/hmac"
	"crypto/sha256"
	"encoding/binary"
	"strings"
	"testing"
	"time"
)

func signedPacket(key string, seconds int64) []byte {
	p := make([]byte, envelopeSize+1)
	copy(p, "AKR1")
	binary.BigEndian.PutUint64(p[4:12], uint64(seconds))
	p[12] = 1
	p[60] = 0x80
	m := hmac.New(sha256.New, []byte(key))
	m.Write(p[:28])
	m.Write(p[60:])
	copy(p[28:60], m.Sum(nil))
	return p
}

func TestAuthentication(t *testing.T) {
	key := strings.Repeat("k", 32)
	now := time.Unix(2000, 0)
	for _, tc := range []struct {
		name   string
		packet []byte
	}{
		{"unsigned", []byte{0x80}},
		{"wrong key", signedPacket(strings.Repeat("x", 32), 2000)},
		{"expired", signedPacket(key, 1969)},
		{"future", signedPacket(key, 2031)},
	} {
		t.Run(tc.name, func(t *testing.T) {
			a := authenticator{key: []byte(key)}
			if _, ok := a.unwrap(tc.packet, now); ok {
				t.Fatal("authenticated invalid packet")
			}
		})
	}
	a := authenticator{key: []byte(key)}
	p := signedPacket(key, 2000)
	if body, ok := a.unwrap(p, now); !ok || len(body) != 1 {
		t.Fatal("valid packet rejected")
	}
	if _, ok := a.unwrap(p, now); ok {
		t.Fatal("replay accepted")
	}
	p[60] ^= 1
	if _, ok := a.unwrap(p, now); ok {
		t.Fatal("tampering accepted")
	}
}

func TestListenerTrustBoundary(t *testing.T) {
	for _, addr := range []string{"0.0.0.0:4319", "[::]:4319", ":4319", "192.0.2.1:4319"} {
		if _, err := resolveListen(addr, ""); err == nil {
			t.Fatalf("unauthenticated listener: %s", addr)
		}
		if _, err := resolveListen(addr, strings.Repeat("k", 32)); err != nil {
			t.Fatal(err)
		}
	}
	if _, err := resolveListen("127.0.0.1:4319", ""); err != nil {
		t.Fatal(err)
	}
	if _, err := resolveListen("127.0.0.1:4319", "short"); err == nil {
		t.Fatal("weak key accepted")
	}
}
