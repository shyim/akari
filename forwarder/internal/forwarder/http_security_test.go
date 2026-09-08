package forwarder

import (
	"context"
	"io"
	"net/http"
	"net/http/httptest"
	"strings"
	"sync/atomic"
	"testing"
)

func TestNoCollectorRedirects(t *testing.T) {
	var leaked atomic.Bool
	target := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) { leaked.Store(true) }))
	defer target.Close()
	for _, status := range []int{301, 302, 303, 307, 308} {
		source := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) { http.Redirect(w, r, target.URL, status) }))
		f := NewHTTP(source.URL, map[string]string{"X-Honeycomb-Team": "secret"})
		err := f.Forward(context.Background(), []byte("{}"), SignalTraces)
		source.Close()
		if err == nil || leaked.Load() {
			t.Fatalf("followed redirect %d", status)
		}
	}
}

type infiniteBody struct{ read int }

func (b *infiniteBody) Read(p []byte) (int, error) {
	for i := range p {
		p[i] = '\n'
	}
	b.read += len(p)
	return len(p), nil
}
func (*infiniteBody) Close() error { return nil }

type roundTripFunc func(*http.Request) (*http.Response, error)

func (f roundTripFunc) RoundTrip(r *http.Request) (*http.Response, error) { return f(r) }

func TestCollectorBodyLimit(t *testing.T) {
	for _, status := range []int{200, 500} {
		body := &infiniteBody{}
		f := NewHTTP("http://localhost", nil)
		f.client.Transport = roundTripFunc(func(*http.Request) (*http.Response, error) {
			return &http.Response{StatusCode: status, Body: io.ReadCloser(body), Header: make(http.Header)}, nil
		})
		err := f.Forward(context.Background(), []byte("{}"), SignalTraces)
		if body.read > 64*1024 {
			t.Fatal("response limit exceeded")
		}
		if status == 500 && (err == nil || strings.Contains(err.Error(), "\n")) {
			t.Fatal("unsafe collector error")
		}
	}
}
