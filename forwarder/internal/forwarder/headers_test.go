package forwarder

import "testing"

func TestParseHeaders(t *testing.T) {
	tests := []struct {
		name    string
		raw     string
		want    map[string]string
		wantErr bool
	}{
		{
			name: "empty",
			raw:  "",
			want: nil,
		},
		{
			name: "single",
			raw:  "api-key=secret",
			want: map[string]string{"api-key": "secret"},
		},
		{
			name: "multiple",
			raw:  "api-key=secret,x-tenant=acme",
			want: map[string]string{"api-key": "secret", "x-tenant": "acme"},
		},
		{
			name: "whitespace trimmed",
			raw:  " api-key = secret , x-tenant = acme ",
			want: map[string]string{"api-key": "secret", "x-tenant": "acme"},
		},
		{
			name: "percent decoded value",
			raw:  "x-tenant=acme%20corp",
			want: map[string]string{"x-tenant": "acme corp"},
		},
		{
			name: "value with equals sign",
			raw:  "authorization=Basic dXNlcjpwYXNz==",
			want: map[string]string{"authorization": "Basic dXNlcjpwYXNz=="},
		},
		{
			name: "trailing comma ignored",
			raw:  "api-key=secret,",
			want: map[string]string{"api-key": "secret"},
		},
		{
			name:    "missing equals",
			raw:     "api-key",
			wantErr: true,
		},
		{
			name:    "empty key",
			raw:     "=secret",
			wantErr: true,
		},
	}

	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			got, err := ParseHeaders(tt.raw)
			if tt.wantErr {
				if err == nil {
					t.Fatalf("expected error, got nil")
				}
				return
			}
			if err != nil {
				t.Fatalf("unexpected error: %v", err)
			}
			if len(got) != len(tt.want) {
				t.Fatalf("got %v, want %v", got, tt.want)
			}
			for k, v := range tt.want {
				if got[k] != v {
					t.Errorf("header %q = %q, want %q", k, got[k], v)
				}
			}
		})
	}
}
