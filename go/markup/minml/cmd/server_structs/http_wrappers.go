package server_structs

import (
	"log"
	"net/http"
)

// LoggingHandler wraps an http.Handler and logs each request to stderr.
type LoggingHandler struct {
	Handler http.Handler
}

// StatusRecorder captures the HTTP status code written by a handler.
type StatusRecorder struct {
	http.ResponseWriter
	status int
}

// WriteHeader records the status code before delegating to the wrapped writer.
func (r *StatusRecorder) WriteHeader(code int) {
	r.status = code
	r.ResponseWriter.WriteHeader(code)
}

// Flush forwards flushing to the wrapped writer when supported.
func (r *StatusRecorder) Flush() {
	if f, ok := r.ResponseWriter.(http.Flusher); ok {
		f.Flush()
	}
}

// ServeHTTP delegates to the wrapped handler and logs the final status code.
func (l *LoggingHandler) ServeHTTP(w http.ResponseWriter, r *http.Request) {
	rec := &StatusRecorder{ResponseWriter: w, status: 200}
	l.Handler.ServeHTTP(rec, r)
	log.Printf("%s %s %d", r.Method, r.URL.Path, rec.status)
}
