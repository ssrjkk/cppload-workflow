package internal

import (
	"net/http"
	"os"
	"strings"
)

// AuthConfig controls access to the platform API.
type AuthConfig struct {
	APIKey string
}

func NewAuthConfigFromEnv() AuthConfig {
	key := strings.TrimSpace(os.Getenv("VOLLEY_API_KEY"))
	if key == "" {
		key = "dev-local-key"
	}
	return AuthConfig{APIKey: key}
}

func (a AuthConfig) Middleware(next http.Handler) http.Handler {
	return http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		if r.URL.Path == "/health" {
			next.ServeHTTP(w, r)
			return
		}
		if a.APIKey == "" {
			next.ServeHTTP(w, r)
			return
		}
		provided := strings.TrimSpace(r.Header.Get("X-API-Key"))
		if provided != a.APIKey {
			http.Error(w, "forbidden", http.StatusForbidden)
			return
		}
		next.ServeHTTP(w, r)
	})
}
