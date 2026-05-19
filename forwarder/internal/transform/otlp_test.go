package transform

import (
	"encoding/json"
	"testing"

	"github.com/vmihailenco/msgpack/v5"
)

func makeDatagram(serviceName string, traceID []byte, spans ...Span) Datagram {
	return Datagram{
		Version:     1,
		ServiceName: serviceName,
		TraceID:     traceID,
		Spans:       spans,
	}
}

func makeTraceID() []byte {
	b := make([]byte, 32)
	for i := range b {
		b[i] = 'a'
	}
	return b
}

func makeSpanID() []byte {
	b := make([]byte, 16)
	for i := range b {
		b[i] = 'b'
	}
	return b
}

func TestBasicSpan(t *testing.T) {
	dg := makeDatagram("test-svc", makeTraceID(), Span{
		SpanID:       makeSpanID(),
		ParentSpanID: makeSpanID(),
		Name:         "GET /index",
		Kind:         2,
		StatusCode:   0,
		StartNs:      1000,
		EndNs:        2000,
	})

	data, err := msgpack.Marshal(dg)
	if err != nil {
		t.Fatal(err)
	}

	out, err := Transform(data)
	if err != nil {
		t.Fatal(err)
	}

	var v map[string]interface{}
	if err := json.Unmarshal(out, &v); err != nil {
		t.Fatalf("invalid JSON: %v", err)
	}

	// Verify structure
	rss := v["resourceSpans"].([]interface{})
	rs := rss[0].(map[string]interface{})
	resource := rs["resource"].(map[string]interface{})
	attrs := resource["attributes"].([]interface{})
	svcAttr := attrs[0].(map[string]interface{})
	if svcAttr["key"] != "service.name" {
		t.Fatal("missing service.name")
	}
	if svcAttr["value"].(map[string]interface{})["stringValue"] != "test-svc" {
		t.Fatal("wrong service.name")
	}

	ss := rs["scopeSpans"].([]interface{})
	scopeSpan := ss[0].(map[string]interface{})
	spans := scopeSpan["spans"].([]interface{})
	if len(spans) != 1 {
		t.Fatalf("expected 1 span, got %d", len(spans))
	}
}

func TestExceptionEvent(t *testing.T) {
	dg := makeDatagram("test-svc", makeTraceID(), Span{
		SpanID:           makeSpanID(),
		Name:             "PDO::execute",
		Kind:             3,
		StartNs:          1000,
		EndNs:            2000,
		ExceptionType:    "PDOException",
		ExceptionMessage: "SQLSTATE[42S02]",
		ExceptionTimeNs:  1500,
	})

	data, err := msgpack.Marshal(dg)
	if err != nil {
		t.Fatal(err)
	}

	out, err := Transform(data)
	if err != nil {
		t.Fatal(err)
	}

	var v map[string]interface{}
	json.Unmarshal(out, &v)

	rss := v["resourceSpans"].([]interface{})
	rs := rss[0].(map[string]interface{})
	ss := rs["scopeSpans"].([]interface{})
	scopeSpan := ss[0].(map[string]interface{})
	spans := scopeSpan["spans"].([]interface{})
	span := spans[0].(map[string]interface{})

	events, ok := span["events"].([]interface{})
	if !ok || len(events) != 1 {
		t.Fatalf("expected 1 event, got %v", span["events"])
	}
	evt := events[0].(map[string]interface{})
	if evt["name"] != "exception" {
		t.Errorf("event name = %v", evt["name"])
	}
	evtAttrs := evt["attributes"].([]interface{})
	foundType := false
	foundMsg := false
	for _, a := range evtAttrs {
		attr := a.(map[string]interface{})
		k := attr["key"].(string)
		v := attr["value"].(map[string]interface{})["stringValue"].(string)
		if k == "exception.type" && v == "PDOException" {
			foundType = true
		}
		if k == "exception.message" && v == "SQLSTATE[42S02]" {
			foundMsg = true
		}
	}
	if !foundType {
		t.Error("missing exception.type in event")
	}
	if !foundMsg {
		t.Error("missing exception.message in event")
	}
}

func TestRootSpanRuntimeEnv(t *testing.T) {
	dg := makeDatagram("test-svc", makeTraceID(), Span{
		SpanID:        makeSpanID(),
		Name:          "GET /index",
		Kind:          2,
		StartNs:       1000,
		EndNs:         2000,
		PhpVersion:    "8.5.6",
		PhpSapi:       "fpm-fcgi",
		OpcacheOn:     1,
		OpcacheMemMb:  128,
		MaxExecTime:   60,
		MemoryLimitMb: 512,
		PeakMemBytes:  10485760,
		DisplayErrors: 0,
	})

	data, err := msgpack.Marshal(dg)
	if err != nil {
		t.Fatal(err)
	}

	out, err := Transform(data)
	if err != nil {
		t.Fatal(err)
	}

	raw := string(out)

	checks := []string{
		"php.version",
		"8.5.6",
		"php.sapi",
		"fpm-fcgi",
		"php.opcache.enabled",
		"php.opcache.memory_consumption_mb",
		"128",
		"php.max_execution_time",
		"php.memory_limit_mb",
		"php.memory.peak_usage_bytes",
		"10485760",
		"php.display_errors",
	}
	for _, s := range checks {
		if !contains(raw, s) {
			t.Errorf("missing %q in output", s)
		}
	}
}

func TestRootSpanFramework(t *testing.T) {
	dg := makeDatagram("test-svc", makeTraceID(), Span{
		SpanID:         makeSpanID(),
		Name:           "GET App\\Controller\\BlogController::index",
		Kind:           2,
		StartNs:        1000,
		EndNs:          2000,
		HttpRoute:      "app_blog_index",
		HttpController: "App\\Controller\\BlogController::index",
	})

	data, err := msgpack.Marshal(dg)
	if err != nil {
		t.Fatal(err)
	}

	out, err := Transform(data)
	if err != nil {
		t.Fatal(err)
	}

	raw := string(out)

	checks := []string{
		"http.route",
		"app_blog_index",
		"http.controller",
		"App\\\\Controller\\\\BlogController::index",
	}
	for _, s := range checks {
		if !contains(raw, s) {
			t.Errorf("missing %q in output", s)
		}
	}
}

func TestMsgpackRoundtrip(t *testing.T) {
	// Verify all new fields survive a msgpack marshal → unmarshal roundtrip
	original := Span{
		SpanID:           makeSpanID(),
		Name:             "test",
		ExceptionType:    "Error",
		ExceptionMessage: "oops",
		ExceptionTimeNs:  999,
		PhpVersion:       "8.5.6",
		PhpSapi:          "cli",
		HttpRoute:        "home",
		HttpController:   "HomeController",
		OpcacheOn:        1,
		OpcacheMemMb:     256,
		MaxExecTime:      30,
		MemoryLimitMb:    1024,
		PeakMemBytes:     65536,
		DisplayErrors:    1,
	}

	data, err := msgpack.Marshal(original)
	if err != nil {
		t.Fatal(err)
	}

	var decoded Span
	if err := msgpack.Unmarshal(data, &decoded); err != nil {
		t.Fatal(err)
	}

	if decoded.ExceptionType != "Error" {
		t.Error("ExceptionType not preserved")
	}
	if decoded.ExceptionMessage != "oops" {
		t.Error("ExceptionMessage not preserved")
	}
	if decoded.ExceptionTimeNs != 999 {
		t.Error("ExceptionTimeNs not preserved")
	}
	if decoded.PhpVersion != "8.5.6" {
		t.Error("PhpVersion not preserved")
	}
	if decoded.HttpRoute != "home" {
		t.Error("HttpRoute not preserved")
	}
	if decoded.HttpController != "HomeController" {
		t.Error("HttpController not preserved")
	}
}

func contains(s, sub string) bool {
	return len(s) >= len(sub) && searchString(s, sub)
}

func searchString(s, sub string) bool {
	for i := 0; i <= len(s)-len(sub); i++ {
		if s[i:i+len(sub)] == sub {
			return true
		}
	}
	return false
}
