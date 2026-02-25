package main

import (
	"fmt"
	"io/fs"
	"log"
	"net/http"
	"os"
	"os/signal"
	"path/filepath"
	"strings"
	"sync"
	"syscall"
	"time"

	"github.com/fsnotify/fsnotify"
)

// reloadScript is appended to every rendered HTML file.
// It opens an SSE connection to /___reload and reloads the page on events.
const reloadScript = `<script>
(function() {
  var delay = 1000;
  function connect() {
    var es = new EventSource("/___reload");
    es.addEventListener("reload", function() {
      window.location.reload();
    });
    es.onerror = function() {
      es.close();
      setTimeout(function() {
        connect();
        delay = Math.min(delay * 2, 10000);
      }, delay);
    };
    es.onopen = function() { delay = 1000; };
  }
  connect();
})();
</script>
`

// sseClients tracks connected SSE clients for live reload notifications.
type sseClients struct {
	mu      sync.Mutex
	clients []chan struct{}
}

// add registers a new SSE client and returns its notification channel.
func (s *sseClients) add() chan struct{} {
	s.mu.Lock()
	defer s.mu.Unlock()
	ch := make(chan struct{}, 1)
	s.clients = append(s.clients, ch)
	return ch
}

// remove unregisters an SSE client.
func (s *sseClients) remove(ch chan struct{}) {
	s.mu.Lock()
	defer s.mu.Unlock()
	for i, c := range s.clients {
		if c == ch {
			s.clients = append(s.clients[:i], s.clients[i+1:]...)
			return
		}
	}
}

// notifyAll signals all connected SSE clients to reload.
func (s *sseClients) notifyAll() {
	s.mu.Lock()
	defer s.mu.Unlock()
	for _, ch := range s.clients {
		select {
		case ch <- struct{}{}:
		default:
		}
	}
}

// loggingHandler wraps an http.Handler and logs each request to stderr.
type loggingHandler struct {
	handler http.Handler
}

type statusRecorder struct {
	http.ResponseWriter
	status int
}

func (r *statusRecorder) WriteHeader(code int) {
	r.status = code
	r.ResponseWriter.WriteHeader(code)
}

func (r *statusRecorder) Flush() {
	if f, ok := r.ResponseWriter.(http.Flusher); ok {
		f.Flush()
	}
}

func (l *loggingHandler) ServeHTTP(w http.ResponseWriter, r *http.Request) {
	rec := &statusRecorder{ResponseWriter: w, status: 200}
	l.handler.ServeHTTP(rec, r)
	log.Printf("%s %s %d", r.Method, r.URL.Path, rec.status)
}

// Server starts a local HTTP server that serves converted MinML files.
// It copies the source path (file or directory) into a temporary build directory,
// converts all .minml files to .html with live-reload script injection,
// then serves the result on the given port. Source files are watched for
// changes and automatically re-converted, triggering browser reloads via SSE.
func Server(path string, port string) {
	// Create a temporary build folder
	// The "__" prefix is to prevent potential clashing
	dst := "__build"
	err := os.Mkdir(dst, 0777)
	if err != nil {
		log.Fatal(err)
		return
	}

	// Delete the temp dir on ctrl+C
	sigCh := make(chan os.Signal, 1)
	signal.Notify(sigCh, os.Interrupt, syscall.SIGTERM)
	go func() {
		<-sigCh
		os.RemoveAll(dst)
		os.Exit(0)
	}()

	fi, err := os.Stat(path)
	if err != nil {
		log.Fatal(err)
		return
	}

	// Copy all files from source to the __build dir
	switch mode := fi.Mode(); {
	case mode.IsDir():
		err := os.CopyFS(dst, os.DirFS(path))
		if err != nil {
			log.Fatal(err)
			return
		}
	case mode.IsRegular():
		copyFile(path, dst)
	}

	// Convert all .minml files to .html files
	err = filepath.WalkDir(dst, convertFiles)
	if err != nil {
		log.Fatal(err)
		return
	}

	events := make(chan fsnotify.Event)
	go watchDir(path, events)

	// SSE client tracker
	clients := &sseClients{}

	// Set up HTTP routes
	mux := http.NewServeMux()

	// SSE endpoint for live reload
	mux.HandleFunc("/___reload", func(w http.ResponseWriter, r *http.Request) {
		flusher, ok := w.(http.Flusher)
		if !ok {
			http.Error(w, "streaming not supported", http.StatusInternalServerError)
			return
		}
		w.Header().Set("Content-Type", "text/event-stream")
		w.Header().Set("Cache-Control", "no-cache")
		w.Header().Set("Connection", "keep-alive")
		flusher.Flush()

		ch := clients.add()
		defer clients.remove(ch)

		for {
			select {
			case <-ch:
				fmt.Fprint(w, "event: reload\ndata: ok\n\n")
				flusher.Flush()
			case <-r.Context().Done():
				return
			}
		}
	})

	// File server for everything else
	mux.Handle("/", http.FileServer(http.Dir(dst)))

	// Start HTTP server with request logging
	log.Printf("Serving on http://localhost:%s", port)
	go func() {
		if err := http.ListenAndServe(":"+port, &loggingHandler{handler: mux}); err != nil {
			log.Fatal(err)
		}
	}()

	// Debounce: collect events for 100ms before acting.
	// Deduplicates rapid-fire events from editors.
	debounce := time.NewTimer(0)
	<-debounce.C // drain initial fire

	pending := make(map[string]fsnotify.Op)
	for {
		select {
		case ev := <-events:
			pending[ev.Name] = ev.Op
			debounce.Reset(100 * time.Millisecond)
		case <-debounce.C:
			rebuilt := false
			for file, op := range pending {
				dest := filepath.Join(dst, filepath.Base(file))
				if op&(fsnotify.Remove|fsnotify.Rename) != 0 {
					htmlPath := dest[:len(dest)-len(filepath.Ext(dest))] + ".html"
					_ = os.Remove(dest)
					_ = os.Remove(htmlPath)
					log.Printf("Removed: %s", filepath.Base(file))
					rebuilt = true
					continue
				}
				log.Printf("Rebuilt: %s", filepath.Base(file))
				copyFile(file, dst)
				if err := convertFile(dest); err != nil {
					log.Println(err)
				}
				rebuilt = true
			}
			pending = make(map[string]fsnotify.Op)
			if rebuilt {
				clients.notifyAll()
			}
		}
	}
}

// convertFile converts a single .minml file to .html with live-reload
// script injection, then removes the source .minml file.
// Non-.minml files are ignored.
func convertFile(dest string) error {
	if filepath.Ext(dest) != ".minml" {
		return nil
	}
	htmlPath := dest[:len(dest)-len(".minml")] + ".html"
	out, err := os.Create(htmlPath)
	if err != nil {
		return fmt.Errorf("convert error: %w", err)
	}
	defer out.Close()
	if err := Convert(dest, out); err != nil {
		return fmt.Errorf("convert error: %w", err)
	}
	// Inject live-reload script
	if _, err := out.WriteString(reloadScript); err != nil {
		return fmt.Errorf("inject reload script: %w", err)
	}
	_ = os.Remove(dest)
	return nil
}

// convertFiles is a filepath.WalkDirFunc that converts each .minml file to .html.
func convertFiles(path string, entry fs.DirEntry, err error) error {
	if err != nil {
		return err
	}
	if entry.IsDir() {
		return nil
	}
	return convertFile(path)
}

// watchDir recursively watches a directory for file changes and sends
// events on the channel. Editor temp files are filtered out.
// Handles atomic saves by re-watching after rename/create events.
func watchDir(dir string, changed chan<- fsnotify.Event) {
	watcher, err := fsnotify.NewWatcher()
	if err != nil {
		panic(err)
	}
	defer watcher.Close()

	// Watch directory and all subdirectories
	_ = filepath.WalkDir(dir, func(path string, d fs.DirEntry, err error) error {
		if err != nil {
			return err
		}
		if d.IsDir() {
			if err := watcher.Add(path); err != nil {
				return err
			}
		}
		return nil
	})

	for {
		select {
		case event, ok := <-watcher.Events:
			if !ok {
				return
			}
			if event.Op&(fsnotify.Write|fsnotify.Create|fsnotify.Remove|fsnotify.Rename) != 0 {
				// Skip editor temp files
				if strings.HasSuffix(event.Name, "~") || strings.HasPrefix(filepath.Base(event.Name), ".") {
					continue
				}
				// Watch newly created directories
				if event.Op&fsnotify.Create != 0 {
					if info, err := os.Stat(event.Name); err == nil && info.IsDir() {
						_ = watcher.Add(event.Name)
						continue
					}
				}
				changed <- event
				// Re-add after rename/create to handle atomic saves
				if event.Op&(fsnotify.Create|fsnotify.Rename) != 0 {
					_ = watcher.Add(filepath.Dir(event.Name))
				}
			}
		case err, ok := <-watcher.Errors:
			if !ok {
				return
			}
			log.Println("watch error:", err)
		}
	}
}

// copyFile copies a file into the destination directory, preserving its base name.
func copyFile(path, dst string) {
	data, err := os.ReadFile(path)
	if err != nil {
		log.Fatal(err)
		return
	}
	dest := filepath.Join(dst, filepath.Base(path))
	if err := os.WriteFile(dest, data, 0644); err != nil {
		log.Fatal(err)
		return
	}
}
