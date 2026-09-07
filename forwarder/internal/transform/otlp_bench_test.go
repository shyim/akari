package transform

import (
	"encoding/json"
	"fmt"
	"testing"

	"github.com/vmihailenco/msgpack/v5"
)

// Use the ASCII hex IDs and field sizes emitted by the PHP extension.
func benchmarkDatagram(count int, logs bool) Datagram {
	dg := Datagram{
		Version: 1, ServiceName: "checkout",
		TraceID: []byte("0123456789abcdef0123456789abcdef"),
	}
	for i := 0; i < count; i++ {
		spanID := []byte(fmt.Sprintf("%016x", i+1))
		if logs {
			dg.Logs = append(dg.Logs, LogRecord{
				TimeNs:       1_800_000_000_000_000_000 + uint64(i)*1000,
				SeverityText: "warning", Body: "Payment gateway retry scheduled",
				TraceID: dg.TraceID, SpanID: spanID,
				Attributes: []LogAttr{{Key: "tenant", Value: "acme"}, {Key: "attempt", Value: "2"}},
			})
			continue
		}
		span := Span{
			SpanID: spanID, ParentSpanID: []byte("fedcba9876543210"),
			Name: "PDO::query", Kind: 3,
			StartNs:  1_800_000_000_000_000_000 + uint64(i)*1000,
			EndNs:    1_800_000_000_000_500_000 + uint64(i)*1000,
			Function: "query", Namespace: "PDO", Filepath: "/app/src/Checkout.php", Lineno: 42,
			DbSystem: "mysql", DbName: "shop", DbUser: "app",
			DbStatement: "SELECT id, name, price FROM products WHERE id = ?",
			CustomTags:  []LogAttr{{Key: "tenant", Value: "acme"}},
		}
		if i == 0 {
			span.Name, span.Kind = "GET /checkout", 2
			span.ParentSpanID = nil
			span.DbSystem, span.DbName, span.DbUser, span.DbStatement = "", "", "", ""
			span.HttpMethod, span.UrlPath, span.UrlScheme = "GET", "/checkout", "https"
			span.ServerAddr, span.StatusHttp = "shop.example", 200
			span.PhpVersion, span.PhpSapi, span.Sampled = "8.5.10", "fpm-fcgi", 1
			span.Layers = map[string][]uint64{"app": {300_000, 0}, "db": {200_000, uint64(count - 1)}}
		}
		dg.Spans = append(dg.Spans, span)
	}
	return dg
}

func benchmarkTransform(b *testing.B, logs bool) {
	for _, count := range []int{1, 16, 64} {
		b.Run(fmt.Sprintf("records=%d", count), func(b *testing.B) {
			// Serialization of the input is fixture setup, not forwarder work.
			payload, err := msgpack.Marshal(benchmarkDatagram(count, logs))
			if err != nil {
				b.Fatal(err)
			}
			if len(payload) > 65507 {
				b.Fatal("fixture exceeds the UDP payload limit")
			}
			b.ReportAllocs()
			b.SetBytes(int64(len(payload)))
			var result Result
			for b.Loop() {
				result, err = Transform(payload)
				if err != nil {
					b.Fatal(err)
				}
			}
			// Validate the last result outside the timed loop.
			if logs {
				var output otlpLogsRequest
				if err := json.Unmarshal(result.Logs, &output); err != nil {
					b.Fatal(err)
				}
				if len(output.ResourceLogs) != 1 || len(output.ResourceLogs[0].ScopeLogs) != 1 ||
					len(output.ResourceLogs[0].ScopeLogs[0].LogRecords) != count {
					b.Fatal("unexpected log record count")
				}
			} else {
				var output otlpExportRequest
				if err := json.Unmarshal(result.Traces, &output); err != nil {
					b.Fatal(err)
				}
				if len(output.ResourceSpans) != 1 || len(output.ResourceSpans[0].ScopeSpans) != 1 ||
					len(output.ResourceSpans[0].ScopeSpans[0].Spans) != count {
					b.Fatal("unexpected span count")
				}
			}
		})
	}
}

func BenchmarkTransformTraces(b *testing.B) { benchmarkTransform(b, false) }

func BenchmarkTransformLogs(b *testing.B) { benchmarkTransform(b, true) }
