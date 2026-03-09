package main

import (
	"bytes"
	"fmt"
	"io/fs"
	"log"
	"net/http"
	"os"
	"os/exec"
	"os/signal"
	"path/filepath"
	"runtime"
	"strings"
	"syscall"
	"time"

	"github.com/dedis/matchertext/go/markup/minml/cmd/server_structs"
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
    es.addEventListener("shutdown", function() {
      es.close();
      showStopped();
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
  function showStopped() {
    var overlay = document.createElement("div");
    overlay.style.cssText = "position:fixed;top:0;left:0;width:100%;height:100%;" +
      "background:rgba(0,0,0,0.85);color:#fff;display:flex;align-items:center;" +
      "justify-content:center;z-index:999999;font-family:system-ui,sans-serif";
    overlay.innerHTML = '<div style="text-align:center">' +
      '<h1 style="font-size:2rem;margin:0 0 0.5rem">Server stopped</h1>' +
      '<p style="margin:0;opacity:0.7">The development server is no longer running.</p></div>';
    document.body.appendChild(overlay);
  }
  connect();
})();
</script>
`

// Server starts a local HTTP server that serves converted MinML files.
// It copies the source path (file or directory) into a BuildTarget (in-memory
// or on-disk), converts all .minml files to .html with live-reload script
// injection, then serves the result on the given port. Source files are watched
// for changes and automatically re-converted, triggering browser reloads via SSE.
func Server(path string, port string, noOpen, diskBuild bool, extensions []string) {
	// Create the build target (in-memory or on-disk)
	var target server_structs.BuildTarget
	if diskBuild {
		target = server_structs.NewDiskTarget("__build")
	} else {
		target = server_structs.NewMemoryTarget()
	}
	if err := target.Init(); err != nil {
		log.Fatal(err)
	}

	// SSE client tracker
	clients := &server_structs.SseClients{}

	// On ctrl+C: notify browsers, clean up, exit
	sigCh := make(chan os.Signal, 1)
	signal.Notify(sigCh, os.Interrupt, syscall.SIGTERM)
	go func() {
		<-sigCh
		clients.Notify("shutdown")
		time.Sleep(100 * time.Millisecond) // let SSE flush
		_ = target.Cleanup()
		os.Exit(0)
	}()

	fi, err := os.Stat(path)
	if err != nil {
		_ = target.Cleanup()
		log.Fatal(err)
	}

	// Copy all source files into the build target
	switch mode := fi.Mode(); {
	case mode.IsDir():
		if err := copyFSToTarget(path, target); err != nil {
			_ = target.Cleanup()
			log.Fatal(err)
		}
	case mode.IsRegular():
		if err := copyFileToTarget(path, filepath.Base(path), target); err != nil {
			_ = target.Cleanup()
			log.Fatal(err)
		}
	}

	// Convert all minml files to html files
	err = fs.WalkDir(target.FS(), ".", func(p string, entry fs.DirEntry, err error) error {
		if err != nil {
			return err
		}
		if entry.IsDir() {
			return nil
		}
		return convertFile(target, p, extensions)
	})
	if err != nil {
		_ = target.Cleanup()
		log.Fatal(err)
	}

	events := make(chan fsnotify.Event)
	watchPath := path
	if !fi.IsDir() {
		watchPath = filepath.Dir(path)
	}
	go watchDir(watchPath, events)

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

		ch := clients.Add()
		defer clients.Remove(ch)

		for {
			select {
			case event := <-ch:
				fmt.Fprintf(w, "event: %s\ndata: ok\n\n", event)
				flusher.Flush()
			case <-r.Context().Done():
				return
			}
		}
	})

	// File server for everything else
	mux.Handle("/", http.FileServer(http.FS(target.FS())))

	// Start HTTP server with request logging
	log.Printf("Serving on http://localhost:%s", port)
	go func() {
		if err := http.ListenAndServe(":"+port, &server_structs.LoggingHandler{Handler: mux}); err != nil {
			log.Fatal(err)
		}
	}()

	// Auto open the link in the browser
	if !noOpen {
		openBrowser("http://localhost:" + port)
	}

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
				relPath := filepath.Base(file)
				if op&(fsnotify.Remove|fsnotify.Rename) != 0 {
					htmlPath := relPath[:len(relPath)-len(filepath.Ext(relPath))] + ".html"
					_ = target.RemoveFile(relPath)
					_ = target.RemoveFile(htmlPath)
					log.Printf("Removed: %s", relPath)
					rebuilt = true
					continue
				}
				log.Printf("Rebuilt: %s", relPath)
				if err := copyFileToTarget(file, relPath, target); err != nil {
					log.Println(err)
					continue
				}
				if err := convertFile(target, relPath, extensions); err != nil {
					log.Println(err)
				}
				rebuilt = true
			}
			pending = make(map[string]fsnotify.Op)
			if rebuilt {
				clients.Notify("reload")
			}
		}
	}
}

// convertFile reads a file from the BuildTarget, converts it from MinML to
// HTML with live-reload script injection, writes the result back, and removes
// the source file. Non-matching extensions are ignored.
func convertFile(target server_structs.BuildTarget, relPath string, extensions []string) error {
	isMinml, extension := IsMinmlFile(relPath, extensions)
	if !isMinml {
		return nil
	}

	// Read source from the build target
	data, err := fs.ReadFile(target.FS(), relPath)
	if err != nil {
		return fmt.Errorf("reading %s: %w", relPath, err)
	}

	// Convert MinML to HTML
	var buf bytes.Buffer
	if err := convertFromReader(bytes.NewReader(data), &buf, relPath); err != nil {
		return fmt.Errorf("converting %s: %w", relPath, err)
	}

	// Inject live-reload script
	buf.WriteString(reloadScript)

	// Write HTML output to the build target
	htmlPath := relPath[:len(relPath)-len(extension)] + "html"
	if err := target.WriteFile(htmlPath, buf.Bytes()); err != nil {
		return fmt.Errorf("writing %s: %w", htmlPath, err)
	}

	// Remove the source file
	_ = target.RemoveFile(relPath)
	return nil
}

// copyFSToTarget recursively copies all files from a directory into the BuildTarget.
func copyFSToTarget(srcDir string, target server_structs.BuildTarget) error {
	return fs.WalkDir(os.DirFS(srcDir), ".", func(p string, d fs.DirEntry, err error) error {
		if err != nil || d.IsDir() {
			return err
		}
		return copyFileToTarget(filepath.Join(srcDir, p), p, target)
	})
}

// copyFileToTarget reads a file from disk and writes it into the BuildTarget
// at the given relative path.
func copyFileToTarget(srcPath, relPath string, target server_structs.BuildTarget) error {
	data, err := os.ReadFile(srcPath)
	if err != nil {
		return fmt.Errorf("reading %s: %w", srcPath, err)
	}
	return target.WriteFile(relPath, data)
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

// openBrowser opens the launched web servers index.html webpage
func openBrowser(url string) {
	var cmd *exec.Cmd
	switch runtime.GOOS {
	case "darwin":
		cmd = exec.Command("open", url)
	case "linux":
		cmd = exec.Command("xdg-open", url)
	case "windows":
		cmd = exec.Command("cmd", "/c", "start", url)
	default:
		return
	}
	_ = cmd.Start()
}
