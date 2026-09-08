package transform

import (
	"bytes"
	"encoding/binary"
	"errors"
	"math"
	"strings"
	"unicode"
)

const MaxDatagramSize = 60 * 1024

var errInvalid = errors.New("invalid or oversized telemetry datagram")

// ValidateWire walks the entire MessagePack value without allocating. Do this
// BEFORE using the reflection decoder: declared array lengths can otherwise
// cause allocations even when the input ends immediately after the length.
func ValidateWire(data []byte) error {
	if len(data) == 0 || len(data) > MaxDatagramSize || data[0]&0xf0 != 0x80 {
		return errInvalid
	}
	p := wireParser{data: data, nodes: 8192}
	if !p.value(0) || p.pos != len(data) {
		return errInvalid
	}
	return nil
}

type wireParser struct {
	data       []byte
	pos, nodes int
}

func (p *wireParser) take(n int) bool {
	if n < 0 || n > len(p.data)-p.pos {
		return false
	}
	p.pos += n
	return true
}

func (p *wireParser) length(n int) (int, bool) {
	start := p.pos
	if !p.take(n) {
		return 0, false
	}
	var v uint64
	switch n {
	case 1:
		v = uint64(p.data[start])
	case 2:
		v = uint64(binary.BigEndian.Uint16(p.data[start:]))
	case 4:
		v = uint64(binary.BigEndian.Uint32(p.data[start:]))
	}
	if v > MaxDatagramSize {
		return 0, false
	}
	return int(v), true
}

func (p *wireParser) collection(n, depth int, isMap bool) bool {
	if n > 512 || (isMap && n > 128) {
		return false
	}
	var keys [128][]byte
	for i := 0; i < n; i++ {
		if isMap {
			// Protocol maps only have string keys. Reject duplicates, including
			// keys encoded with different MessagePack string length forms.
			key, ok := p.key()
			if !ok {
				return false
			}
			for j := 0; j < i; j++ {
				if bytes.Equal(keys[j], key) {
					return false
				}
			}
			keys[i] = key
		}
		if !p.value(depth + 1) {
			return false
		}
	}
	return true
}

func (p *wireParser) key() ([]byte, bool) {
	if !p.take(1) {
		return nil, false
	}
	c := p.data[p.pos-1]
	n := 0
	ok := true
	switch {
	case c&0xe0 == 0xa0:
		n = int(c & 31)
	case c == 0xd9:
		n, ok = p.length(1)
	case c == 0xda:
		n, ok = p.length(2)
	case c == 0xdb:
		n, ok = p.length(4)
	default:
		return nil, false
	}
	start := p.pos
	if !ok || n == 0 || n > 256 || !p.take(n) {
		return nil, false
	}
	return p.data[start:p.pos], true
}

func (p *wireParser) value(depth int) bool {
	p.nodes--
	if depth > 8 || p.nodes < 0 || !p.take(1) {
		return false
	}
	c := p.data[p.pos-1]
	switch {
	case c <= 0x7f || c >= 0xe0 || c == 0xc0 || c == 0xc2 || c == 0xc3:
		return true
	case c&0xe0 == 0xa0:
		return p.take(int(c & 31))
	case c&0xf0 == 0x90:
		return p.collection(int(c&15), depth, false)
	case c&0xf0 == 0x80:
		return p.collection(int(c&15), depth, true)
	}
	switch c {
	case 0xcc, 0xd0:
		return p.take(1)
	case 0xcd, 0xd1:
		return p.take(2)
	case 0xce, 0xd2, 0xca:
		return p.take(4)
	case 0xcf, 0xd3, 0xcb:
		return p.take(8)
	case 0xc4, 0xc5, 0xc6, 0xd9, 0xda, 0xdb:
		width := 1
		if c == 0xc5 || c == 0xda {
			width = 2
		}
		if c == 0xc6 || c == 0xdb {
			width = 4
		}
		n, ok := p.length(width)
		return ok && n <= 4096 && p.take(n)
	case 0xdc, 0xdd, 0xde, 0xdf:
		width := 2
		if c == 0xdd || c == 0xdf {
			width = 4
		}
		n, ok := p.length(width)
		return ok && p.collection(n, depth, c == 0xde || c == 0xdf)
	}
	return false // Extension values are not part of this protocol.
}

func validID(id []byte, size int, optional bool) bool {
	if optional && (len(id) == 0 || (len(id) == size && isZeroID(id))) {
		return true
	}
	if len(id) != size || isZeroID(id) {
		return false
	}
	for _, c := range id {
		if !((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f')) {
			return false
		}
	}
	return true
}

func validateDatagram(d *Datagram) error {
	if d.Version != 1 || len(d.ServiceName) == 0 || len(d.ServiceName) > 256 || strings.IndexFunc(d.ServiceName, unicode.IsControl) >= 0 ||
		!validID(d.TraceID, 32, len(d.Spans) == 0) {
		return errInvalid
	}
	for _, s := range d.Spans {
		if !validID(s.SpanID, 16, false) || !validID(s.ParentSpanID, 16, true) ||
			!validID(s.LinkedTraceID, 32, true) || !validID(s.LinkedSpanID, 16, true) ||
			s.Kind > 5 || s.StatusCode > 2 || s.EndNs < s.StartNs || s.EndNs > math.MaxInt64 ||
			s.ServerPort < 0 || s.ServerPort > 65535 || s.HttpPort < 0 || s.HttpPort > 65535 {
			return errInvalid
		}
		for _, layer := range s.Layers {
			if len(layer) != 2 {
				return errInvalid
			}
		}
	}
	for _, l := range d.Logs {
		if !validID(l.TraceID, 32, true) || !validID(l.SpanID, 16, true) || l.TimeNs > math.MaxInt64 {
			return errInvalid
		}
	}
	return nil
}
