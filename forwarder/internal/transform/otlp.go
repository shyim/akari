package transform

import (
	"encoding/json"
	"fmt"
	"os"
	"runtime"
	"strings"

	"github.com/vmihailenco/msgpack/v5"
)

// Datagram is the wire format sent by the PHP extension over UDP. A datagram
// carries either spans (from udp_export_spans) or log records (from
// udp_export_logs), never both — but the type tolerates either being present.
type Datagram struct {
	Version     uint8       `msgpack:"v"`
	ServiceName string      `msgpack:"sn"`
	TraceID     []byte      `msgpack:"ti"`
	Spans       []Span      `msgpack:"sp,omitempty"`
	Logs        []LogRecord `msgpack:"lg,omitempty"`
}

// LogAttr is a single context key/value (always string-valued from PHP).
type LogAttr struct {
	Key   string `msgpack:"k"`
	Value string `msgpack:"v"`
}

// LogRecord mirrors the C profiler_log_record_t msgpack encoding.
type LogRecord struct {
	TimeNs       uint64    `msgpack:"tn"`
	SeverityText string    `msgpack:"sv"`
	Body         string    `msgpack:"bo"`
	TraceID      []byte    `msgpack:"tr,omitempty"`
	SpanID       []byte    `msgpack:"sp,omitempty"`
	Attributes   []LogAttr `msgpack:"at,omitempty"`
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

	// Root span: full CLI command line (process.command_line)
	ProcessCommandLine string `msgpack:"pc,omitempty"`
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

// OTLP logs JSON structures (ExportLogsServiceRequest subset).
type otlpLogsRequest struct {
	ResourceLogs []otlpResourceLogs `json:"resourceLogs"`
}

type otlpResourceLogs struct {
	Resource  otlpResource   `json:"resource"`
	ScopeLogs []otlpScopeLog `json:"scopeLogs"`
}

type otlpScopeLog struct {
	Scope      otlpScope       `json:"scope"`
	LogRecords []otlpLogRecord `json:"logRecords"`
}

type otlpLogRecord struct {
	TimeUnixNano   string         `json:"timeUnixNano"`
	SeverityNumber int            `json:"severityNumber"`
	SeverityText   string         `json:"severityText,omitempty"`
	Body           otlpAnyValue   `json:"body"`
	TraceID        string         `json:"traceId,omitempty"`
	SpanID         string         `json:"spanId,omitempty"`
	Attributes     []otlpKeyValue `json:"attributes"`
}

// severityNumber maps a PSR-3 / common severity text onto the OTLP
// SeverityNumber buckets (TRACE=1, DEBUG=5, INFO=9, WARN=13, ERROR=17,
// FATAL=21). Unknown text defaults to INFO.
func severityNumber(text string) int {
	switch strings.ToLower(strings.TrimSpace(text)) {
	case "trace":
		return 1
	case "debug":
		return 5
	case "info", "notice":
		return 9
	case "warn", "warning":
		return 13
	case "error", "err", "critical", "crit", "alert":
		return 17
	case "fatal", "emergency", "emerg":
		return 21
	default:
		return 9
	}
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

// Result holds the OTLP JSON bodies produced from one datagram. Each is nil
// when the datagram carried no records of that signal, so the caller only
// POSTs the endpoints that have data.
type Result struct {
	Traces []byte
	Logs   []byte
}

// buildResourceAttrs builds the OTLP resource attributes shared by all signals.
func buildResourceAttrs(serviceName string) []otlpKeyValue {
	attrs := []otlpKeyValue{
		{Key: "service.name", Value: strVal(serviceName)},
		{Key: "telemetry.sdk.name", Value: strVal("opentelemetry")},
		{Key: "telemetry.sdk.language", Value: strVal("php")},
		{Key: "telemetry.sdk.version", Value: strVal("0.1.0")},
	}
	if cachedHostname != "" {
		attrs = append(attrs, otlpKeyValue{Key: "host.name", Value: strVal(cachedHostname)})
	}
	if cachedOS != "" {
		attrs = append(attrs, otlpKeyValue{Key: "os.type", Value: strVal(cachedOS)})
	}
	return attrs
}

// Transform decodes a msgpack datagram into OTLP JSON bodies (traces and/or logs).
func Transform(data []byte) (Result, error) {
	var dg Datagram
	if err := msgpack.Unmarshal(data, &dg); err != nil {
		return Result{}, fmt.Errorf("msgpack decode: %w", err)
	}

	if dg.Version != 1 {
		return Result{}, fmt.Errorf("unsupported protocol version: %d", dg.Version)
	}

	traceID := string(dg.TraceID)
	resAttrs := buildResourceAttrs(dg.ServiceName)

	var result Result

	if len(dg.Logs) > 0 {
		logsJSON, err := buildLogsRequest(dg.Logs, resAttrs)
		if err != nil {
			return Result{}, err
		}
		result.Logs = logsJSON
	}

	if len(dg.Spans) == 0 {
		return result, nil
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

		// Root-span attributes (HTTP for SERVER roots, command line for CLI roots,
		// plus shared runtime annotations). A SERVER span is always the root; a CLI
		// root is INTERNAL but is the only INTERNAL span carrying php.sapi. HTTP
		// fields stay individually guarded so they don't appear on a CLI root.
		if s.Kind == 2 || s.PhpSapi != "" {
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
			if s.ProcessCommandLine != "" {
				attrs = append(attrs, otlpKeyValue{Key: "process.command_line", Value: strVal(s.ProcessCommandLine)})
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

	tracesJSON, err := json.Marshal(req)
	if err != nil {
		return Result{}, err
	}
	result.Traces = tracesJSON
	return result, nil
}

// buildLogsRequest converts decoded log records into an OTLP logs JSON body.
func buildLogsRequest(logs []LogRecord, resAttrs []otlpKeyValue) ([]byte, error) {
	records := make([]otlpLogRecord, 0, len(logs))
	for _, lr := range logs {
		rec := otlpLogRecord{
			TimeUnixNano:   fmt.Sprintf("%d", lr.TimeNs),
			SeverityNumber: severityNumber(lr.SeverityText),
			SeverityText:   lr.SeverityText,
			Body:           strVal(lr.Body),
		}
		if len(lr.TraceID) == 32 && !isZeroID(lr.TraceID) {
			rec.TraceID = string(lr.TraceID)
		}
		if len(lr.SpanID) == 16 && !isZeroID(lr.SpanID) {
			rec.SpanID = string(lr.SpanID)
		}
		attrs := make([]otlpKeyValue, 0, len(lr.Attributes))
		for _, a := range lr.Attributes {
			attrs = append(attrs, otlpKeyValue{Key: a.Key, Value: strVal(a.Value)})
		}
		rec.Attributes = attrs
		records = append(records, rec)
	}

	req := otlpLogsRequest{
		ResourceLogs: []otlpResourceLogs{{
			Resource: otlpResource{Attributes: resAttrs},
			ScopeLogs: []otlpScopeLog{{
				Scope:      otlpScope{Name: "akari", Version: "0.1.0"},
				LogRecords: records,
			}},
		}},
	}

	return json.Marshal(req)
}
