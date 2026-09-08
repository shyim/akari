package transform

import (
	"bytes"
	"github.com/vmihailenco/msgpack/v5"
	"runtime"
	"testing"
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
