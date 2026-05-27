package transform

import (
	"encoding/json"
	"fmt"
	"os"
	"runtime"

	"github.com/vmihailenco/msgpack/v5"
)

// Datagram is the wire format sent by the PHP extension over UDP.
type Datagram struct {
	Version     uint8  `msgpack:"v"`
	ServiceName string `msgpack:"sn"`
	TraceID     []byte `msgpack:"ti"`
	Spans       []Span `msgpack:"sp"`
}

// Span represents a single span in the datagram.
type Span struct {
	SpanID       []byte `msgpack:"s"`
	ParentSpanID []byte `msgpack:"p"`
	Name         string `msgpack:"n"`
	Kind         uint8  `msgpack:"k"`
	StatusCode   uint8  `msgpack:"sc"`
	StatusMsg    string `msgpack:"sm"`
	StartNs      uint64 `msgpack:"ts"`
	EndNs        uint64 `msgpack:"te"`
	Function     string `msgpack:"fn"`
	Filepath     string `msgpack:"fp"`
	Namespace    string `msgpack:"ns"`
	Lineno       uint32 `msgpack:"ln"`

	// HTTP attrs (shared by root SERVER span and curl CLIENT span)
	HttpMethod string `msgpack:"hm,omitempty"`  // http.request.method
	UrlPath    string `msgpack:"up,omitempty"`  // url.path (root span)
	UrlScheme  string `msgpack:"us,omitempty"`  // url.scheme (root span)
	ServerAddr string `msgpack:"sa,omitempty"`  // server.address (root span)
	ServerPort int    `msgpack:"pt,omitempty"`  // server.port (root span)
	StatusHttp int    `msgpack:"hc,omitempty"`  // http.response.status_code
	HttpUrl    string `msgpack:"hu,omitempty"`  // url.full (curl span)
	HttpAddr   string `msgpack:"ha,omitempty"`  // server.address (curl span)
	HttpPort   int    `msgpack:"hp,omitempty"`  // server.port (curl span)

	// Database attrs (PDO spans, kind=3 CLIENT)
	DbSystem    string `msgpack:"ds,omitempty"`
	DbName      string `msgpack:"dn,omitempty"`
	DbUser      string `msgpack:"du,omitempty"`
	DbStatement string `msgpack:"dq,omitempty"`

	// Messaging attrs (AMQP spans)
	MsgSystem     string `msgpack:"ms,omitempty"`
	MsgOperation  string `msgpack:"mo,omitempty"`
	MsgDest       string `msgpack:"md,omitempty"`
	LinkedTraceID []byte `msgpack:"lt,omitempty"`
	LinkedSpanID  []byte `msgpack:"ls,omitempty"`

	// Template attrs (Twig spans)
	TemplateEngine string `msgpack:"tg,omitempty"`
	TemplateName   string `msgpack:"tn,omitempty"`
	TemplateBlock  string `msgpack:"tb,omitempty"`

	// Exception event (child spans)
	ExceptionType    string `msgpack:"et,omitempty"`
	ExceptionMessage string `msgpack:"em,omitempty"`
	ExceptionTimeNs  uint64 `msgpack:"ev,omitempty"`

	// Root span: PHP runtime environment
	PhpVersion    string `msgpack:"pv,omitempty"`
	PhpSapi       string `msgpack:"ps,omitempty"`
	OpcacheOn     uint8  `msgpack:"oc,omitempty"`
	OpcacheMemMb  uint32 `msgpack:"om,omitempty"`
	MaxExecTime   uint32 `msgpack:"mt,omitempty"`
	MemoryLimitMb uint32 `msgpack:"ml,omitempty"`
	PeakMemBytes  uint64 `msgpack:"pm,omitempty"`
	DisplayErrors uint8  `msgpack:"de,omitempty"`

	// Root span: framework-detected route/controller
	HttpRoute      string `msgpack:"hr,omitempty"`
	HttpController string `msgpack:"hk,omitempty"`
}

// OTLP JSON structures (minimal, matching ExportTraceServiceRequest)
type otlpExportRequest struct {
	ResourceSpans []otlpResourceSpan `json:"resourceSpans"`
}

type otlpResourceSpan struct {
	Resource   otlpResource    `json:"resource"`
	ScopeSpans []otlpScopeSpan `json:"scopeSpans"`
}

type otlpResource struct {
	Attributes []otlpKeyValue `json:"attributes"`
}

type otlpScopeSpan struct {
	Scope otlpScope    `json:"scope"`
	Spans []otlpSpan   `json:"spans"`
}

type otlpScope struct {
	Name    string `json:"name"`
	Version string `json:"version"`
}

type otlpSpanLink struct {
	TraceID string `json:"traceId"`
	SpanID  string `json:"spanId"`
}

type otlpSpan struct {
	TraceID      string         `json:"traceId"`
	SpanID       string         `json:"spanId"`
	ParentSpanID string         `json:"parentSpanId,omitempty"`
	Name         string         `json:"name"`
	Kind         int            `json:"kind"`
	StartTimeNs  string         `json:"startTimeUnixNano"`
	EndTimeNs    string         `json:"endTimeUnixNano"`
	Attributes   []otlpKeyValue `json:"attributes"`
	Links        []otlpSpanLink `json:"links,omitempty"`
	Events       []otlpSpanEvent `json:"events,omitempty"`
	Status       otlpStatus     `json:"status"`
}

type otlpSpanEvent struct {
	Name       string         `json:"name"`
	TimeUnixNs string         `json:"timeUnixNano"`
	Attributes []otlpKeyValue `json:"attributes"`
}

type otlpStatus struct {
	Code    int    `json:"code"`
	Message string `json:"message,omitempty"`
}

type otlpKeyValue struct {
	Key   string        `json:"key"`
	Value otlpAnyValue  `json:"value"`
}

type otlpAnyValue struct {
	StringValue *string `json:"stringValue,omitempty"`
	IntValue    *string `json:"intValue,omitempty"`
}

// isZeroID checks if a byte slice is all zeros (null bytes) or all ASCII '0' chars.
func isZeroID(b []byte) bool {
	allNull := true
	allAsciiZero := true
	for _, c := range b {
		if c != 0 {
			allNull = false
		}
		if c != '0' {
			allAsciiZero = false
		}
	}
	return allNull || allAsciiZero
}

func strVal(s string) otlpAnyValue {
	return otlpAnyValue{StringValue: &s}
}

func intVal(v uint64) otlpAnyValue {
	s := fmt.Sprintf("%d", v)
	return otlpAnyValue{IntValue: &s}
}

func intValI(v int) otlpAnyValue {
	s := fmt.Sprintf("%d", v)
	return otlpAnyValue{IntValue: &s}
}

var (
	cachedHostname string
	cachedOS       string
)

func init() {
	cachedHostname, _ = os.Hostname()
	cachedOS = runtime.GOOS
}

// Transform decodes a msgpack datagram and returns OTLP JSON bytes.
func Transform(data []byte) ([]byte, error) {
	var dg Datagram
	if err := msgpack.Unmarshal(data, &dg); err != nil {
		return nil, fmt.Errorf("msgpack decode: %w", err)
	}

	if dg.Version != 1 {
		return nil, fmt.Errorf("unsupported protocol version: %d", dg.Version)
	}

	traceID := string(dg.TraceID)

	// Build resource attributes
	resAttrs := []otlpKeyValue{
		{Key: "service.name", Value: strVal(dg.ServiceName)},
		{Key: "telemetry.sdk.name", Value: strVal("opentelemetry")},
		{Key: "telemetry.sdk.language", Value: strVal("php")},
		{Key: "telemetry.sdk.version", Value: strVal("0.1.0")},
	}
	if cachedHostname != "" {
		resAttrs = append(resAttrs, otlpKeyValue{Key: "host.name", Value: strVal(cachedHostname)})
	}
	if cachedOS != "" {
		resAttrs = append(resAttrs, otlpKeyValue{Key: "os.type", Value: strVal(cachedOS)})
	}

	// Transform spans
	otlpSpans := make([]otlpSpan, 0, len(dg.Spans))
	for _, s := range dg.Spans {
		span := otlpSpan{
			TraceID:     traceID,
			SpanID:      string(s.SpanID),
			Name:        s.Name,
			Kind:        int(s.Kind),
			StartTimeNs: fmt.Sprintf("%d", s.StartNs),
			EndTimeNs:   fmt.Sprintf("%d", s.EndNs),
			Status:      otlpStatus{Code: int(s.StatusCode)},
		}

		// Check if parentSpanID is set (not all-zeros and not all-null-bytes)
		if pid := s.ParentSpanID; len(pid) == 16 && !isZeroID(pid) {
			span.ParentSpanID = string(pid)
		}

		if s.StatusMsg != "" {
			span.Status.Message = s.StatusMsg
		}

		// Span attributes
		var attrs []otlpKeyValue

		if s.Function != "" {
			attrs = append(attrs, otlpKeyValue{Key: "code.function", Value: strVal(s.Function)})
		}
		if s.Namespace != "" {
			attrs = append(attrs, otlpKeyValue{Key: "code.namespace", Value: strVal(s.Namespace)})
		}
		if s.Filepath != "" {
			attrs = append(attrs, otlpKeyValue{Key: "code.filepath", Value: strVal(s.Filepath)})
		}
		if s.Lineno > 0 {
			attrs = append(attrs, otlpKeyValue{Key: "code.lineno", Value: intVal(uint64(s.Lineno))})
		}

		// Database attrs for CLIENT spans
		if s.DbSystem != "" {
			attrs = append(attrs, otlpKeyValue{Key: "db.system", Value: strVal(s.DbSystem)})
		}
		if s.DbName != "" {
			attrs = append(attrs, otlpKeyValue{Key: "db.name", Value: strVal(s.DbName)})
		}
		if s.DbUser != "" {
			attrs = append(attrs, otlpKeyValue{Key: "db.user", Value: strVal(s.DbUser)})
		}
		if s.DbStatement != "" {
			attrs = append(attrs, otlpKeyValue{Key: "db.statement", Value: strVal(s.DbStatement)})
		}

		// HTTP client attrs (curl spans, kind=3)
		if s.HttpUrl != "" {
			attrs = append(attrs, otlpKeyValue{Key: "url.full", Value: strVal(s.HttpUrl)})
		}
		if s.HttpMethod != "" && s.Kind == 3 {
			attrs = append(attrs, otlpKeyValue{Key: "http.request.method", Value: strVal(s.HttpMethod)})
		}
		if s.HttpAddr != "" && s.Kind == 3 {
			attrs = append(attrs, otlpKeyValue{Key: "server.address", Value: strVal(s.HttpAddr)})
		}
		if s.HttpPort > 0 && s.Kind == 3 {
			attrs = append(attrs, otlpKeyValue{Key: "server.port", Value: intValI(s.HttpPort)})
		}
		if s.StatusHttp > 0 && s.Kind == 3 {
			attrs = append(attrs, otlpKeyValue{Key: "http.response.status_code", Value: intValI(s.StatusHttp)})
		}

		// Messaging attrs (AMQP spans)
		if s.MsgSystem != "" {
			attrs = append(attrs, otlpKeyValue{Key: "messaging.system", Value: strVal(s.MsgSystem)})
		}
		if s.MsgOperation != "" {
			attrs = append(attrs, otlpKeyValue{Key: "messaging.operation.name", Value: strVal(s.MsgOperation)})
		}
		if s.MsgDest != "" {
			attrs = append(attrs, otlpKeyValue{Key: "messaging.destination.name", Value: strVal(s.MsgDest)})
		}

		// Template attrs (Twig spans)
		if s.TemplateEngine != "" {
			attrs = append(attrs, otlpKeyValue{Key: "template.engine", Value: strVal(s.TemplateEngine)})
		}
		if s.TemplateName != "" {
			attrs = append(attrs, otlpKeyValue{Key: "template.name", Value: strVal(s.TemplateName)})
		}
		if s.TemplateBlock != "" {
			attrs = append(attrs, otlpKeyValue{Key: "template.block_name", Value: strVal(s.TemplateBlock)})
		}

		// Span links (consumer → producer trace context)
		if len(s.LinkedTraceID) == 32 && len(s.LinkedSpanID) == 16 {
			span.Links = []otlpSpanLink{{
				TraceID: string(s.LinkedTraceID),
				SpanID:  string(s.LinkedSpanID),
			}}
		}

		// Exception event
		if s.ExceptionType != "" {
			eventAttrs := []otlpKeyValue{
				{Key: "exception.type", Value: strVal(s.ExceptionType)},
			}
			if s.ExceptionMessage != "" {
				eventAttrs = append(eventAttrs, otlpKeyValue{Key: "exception.message", Value: strVal(s.ExceptionMessage)})
			}
			span.Events = []otlpSpanEvent{{
				Name:       "exception",
				TimeUnixNs: fmt.Sprintf("%d", s.ExceptionTimeNs),
				Attributes: eventAttrs,
			}}
		}

		// HTTP attrs for root/SERVER spans
		if s.Kind == 2 {
			if s.HttpMethod != "" {
				attrs = append(attrs, otlpKeyValue{Key: "http.request.method", Value: strVal(s.HttpMethod)})
			}
			if s.UrlPath != "" {
				attrs = append(attrs, otlpKeyValue{Key: "url.path", Value: strVal(s.UrlPath)})
			}
			if s.UrlScheme != "" {
				attrs = append(attrs, otlpKeyValue{Key: "url.scheme", Value: strVal(s.UrlScheme)})
			}
			if s.ServerAddr != "" {
				attrs = append(attrs, otlpKeyValue{Key: "server.address", Value: strVal(s.ServerAddr)})
			}
			if s.ServerPort > 0 {
				attrs = append(attrs, otlpKeyValue{Key: "server.port", Value: intValI(s.ServerPort)})
			}
			if s.StatusHttp > 0 {
				attrs = append(attrs, otlpKeyValue{Key: "http.response.status_code", Value: intValI(s.StatusHttp)})
			}
			// Framework-detected route and controller
			if s.HttpRoute != "" {
				attrs = append(attrs, otlpKeyValue{Key: "http.route", Value: strVal(s.HttpRoute)})
			}
			if s.HttpController != "" {
				attrs = append(attrs, otlpKeyValue{Key: "http.controller", Value: strVal(s.HttpController)})
			}
			// PHP runtime environment annotations
			if s.PhpVersion != "" {
				attrs = append(attrs, otlpKeyValue{Key: "php.version", Value: strVal(s.PhpVersion)})
			}
			if s.PhpSapi != "" {
				attrs = append(attrs, otlpKeyValue{Key: "php.sapi", Value: strVal(s.PhpSapi)})
			}
			attrs = append(attrs, otlpKeyValue{Key: "php.opcache.enabled", Value: intVal(uint64(s.OpcacheOn))})
			if s.OpcacheOn == 1 {
				attrs = append(attrs, otlpKeyValue{Key: "php.opcache.memory_consumption_mb", Value: intVal(uint64(s.OpcacheMemMb))})
			}
			if s.MaxExecTime > 0 {
				attrs = append(attrs, otlpKeyValue{Key: "php.max_execution_time", Value: intVal(uint64(s.MaxExecTime))})
			}
			if s.MemoryLimitMb > 0 {
				attrs = append(attrs, otlpKeyValue{Key: "php.memory_limit_mb", Value: intVal(uint64(s.MemoryLimitMb))})
			}
			if s.PeakMemBytes > 0 {
				attrs = append(attrs, otlpKeyValue{Key: "php.memory.peak_usage_bytes", Value: intVal(s.PeakMemBytes)})
			}
			attrs = append(attrs, otlpKeyValue{Key: "php.display_errors", Value: intVal(uint64(s.DisplayErrors))})
		}

		span.Attributes = attrs
		otlpSpans = append(otlpSpans, span)
	}

	req := otlpExportRequest{
		ResourceSpans: []otlpResourceSpan{{
			Resource: otlpResource{Attributes: resAttrs},
			ScopeSpans: []otlpScopeSpan{{
				Scope: otlpScope{Name: "akari", Version: "0.1.0"},
				Spans: otlpSpans,
			}},
		}},
	}

	return json.Marshal(req)
}
