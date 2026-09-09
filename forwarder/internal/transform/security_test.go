package transform

import (
	"bytes"
	"fmt"
	"runtime"
	"testing"

	"github.com/vmihailenco/msgpack/v5"
)

func TestRejectAllocationBombs(t *testing.T) {
	for _, data := range [][]byte{
		{0x82, 0xa1, 'v', 1, 0xa2, 's', 'p', 0xdd, 0, 1, 0x86, 0xa0},
		{0x81, 0xa2, 's', 'p', 0xdd, 0xff, 0xff, 0xff, 0xff},
		{0x81, 0xa2, 's', 'n', 0xdb, 0xff, 0xff, 0xff, 0xff},
		{0x81, 0xa2, 'l', 'y', 0xdf, 0xff, 0xff, 0xff, 0xff},
	} {
		var before, after runtime.MemStats
		runtime.ReadMemStats(&before)
		_, err := Transform(data)
		runtime.ReadMemStats(&after)
		if err == nil {
			t.Fatal("accepted allocation bomb")
		}
		if after.TotalAlloc-before.TotalAlloc > 1024*1024 {
			t.Fatal("allocated before rejecting malformed length")
		}
	}
}

func TestWireBoundaries(t *testing.T) {
	valid, _ := msgpack.Marshal(makeDatagram("svc", makeTraceID(), Span{SpanID: makeSpanID()}))
	if err := ValidateWire(valid); err != nil {
		t.Fatal(err)
	}
	for i := 0; i < len(valid); i++ {
		if ValidateWire(valid[:i]) == nil {
			t.Fatalf("accepted truncated prefix %d", i)
		}
	}
	for _, data := range [][]byte{
		append(append([]byte{}, valid...), 0),
		{0x82, 0xa1, 'v', 1, 0xd9, 1, 'v', 1},
		append([]byte{0x81, 0xa1, 'x'}, bytes.Repeat([]byte{0x91}, 100)...),
		bytes.Repeat([]byte{0}, MaxDatagramSize+1),
	} {
		if ValidateWire(data) == nil {
			t.Fatal("accepted invalid wire value")
		}
	}
}

func TestWireMapKeys(t *testing.T) {
	encodeMap := func(keys []string) []byte {
		data := []byte{0x81, 0xa1, 'x', 0xde, byte(len(keys) >> 8), byte(len(keys))}
		for _, key := range keys {
			encoded, err := msgpack.Marshal(key)
			if err != nil {
				t.Fatal(err)
			}
			data = append(data, encoded...)
			data = append(data, 0xc0)
		}
		return data
	}

	// These distinct keys land in the same hash bucket. A collision must not
	// be mistaken for a duplicate, or hide a duplicate later in the chain.
	colliding := []string{"key-1", "key-117", "key-162", "key-188"}
	if ValidateWire(encodeMap(colliding)) != nil {
		t.Fatal("rejected distinct colliding keys")
	}
	for _, key := range colliding {
		if ValidateWire(encodeMap(append(append([]string{}, colliding...), key))) == nil {
			t.Fatal("accepted duplicate after hash collision")
		}
	}

	keys := make([]string, 129)
	for i := range keys {
		keys[i] = fmt.Sprintf("%0256d", i)
	}
	for _, count := range []int{0, 1, 127, 128, 129} {
		t.Run(fmt.Sprint(count), func(t *testing.T) {
			if accepted := ValidateWire(encodeMap(keys[:count])) == nil; accepted != (count <= 128) {
				t.Fatalf("map with %d maximal-length keys: accepted=%v", count, accepted)
			}
		})
	}

	// Equivalent keys remain duplicates across every string header form.
	forms := [][]byte{{0xa1, 'v'}, {0xd9, 1, 'v'}, {0xda, 0, 1, 'v'}, {0xdb, 0, 0, 0, 1, 'v'}}
	for _, first := range forms {
		for _, second := range forms {
			data := append([]byte{0x82}, first...)
			data = append(data, 0xc0)
			data = append(data, second...)
			data = append(data, 0xc0)
			if ValidateWire(data) == nil {
				t.Fatal("accepted duplicate with different string headers")
			}
		}
	}
}

func TestRejectInvalidSpanFields(t *testing.T) {
	for _, s := range []Span{
		{SpanID: []byte("not an actual id")},
		{SpanID: makeSpanID(), ParentSpanID: []byte("oops")},
		{SpanID: makeSpanID(), Kind: 255},
		{SpanID: makeSpanID(), Kind: 256},
		{SpanID: makeSpanID(), StatusCode: 256},
		{SpanID: makeSpanID(), StatusCode: 255},
		{SpanID: makeSpanID(), StartNs: 20, EndNs: 10},
		{SpanID: makeSpanID(), Layers: map[string][]uint64{"app": {1}}},
	} {
		data, _ := msgpack.Marshal(makeDatagram("svc", makeTraceID(), s))
		if _, err := Transform(data); err == nil {
			t.Fatalf("accepted invalid span: %+v", s)
		}
	}
}

func TestProtocolVersionCannotWrap(t *testing.T) {
	dg := makeDatagram("svc", makeTraceID(), Span{SpanID: makeSpanID()})
	dg.Version = 257
	data, _ := msgpack.Marshal(dg)
	if _, err := Transform(data); err == nil {
		t.Fatal("protocol version wrapped to 1")
	}
}

func FuzzTransform(f *testing.F) {
	valid, _ := msgpack.Marshal(makeDatagram("svc", makeTraceID(), Span{SpanID: makeSpanID()}))
	f.Add(valid)
	f.Add([]byte{0x81, 0xa2, 's', 'p', 0xdd, 0xff, 0xff, 0xff, 0xff})
	f.Fuzz(func(t *testing.T, data []byte) { _, _ = Transform(data) })
}
