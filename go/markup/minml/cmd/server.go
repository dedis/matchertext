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
	"syscall"
	"time"

	"github.com/fsnotify/fsnotify"
)

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

	// Copy all files from source dst to the new temp __build dst
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

	// Serve the build directory
	log.Printf("Serving %s on http://localhost:%s", dst, port)
	http.Handle("/", http.FileServer(http.Dir(dst)))
	go func() {
		if err := http.ListenAndServe(":"+port, nil); err != nil {
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
			for file, op := range pending {
				dest := filepath.Join(dst, filepath.Base(file))
				if op&(fsnotify.Remove|fsnotify.Rename) != 0 {
					// Delete the output file
					htmlPath := dest[:len(dest)-len(filepath.Ext(dest))] + ".html"
					_ = os.Remove(dest)
					_ = os.Remove(htmlPath)
					log.Printf("Removed: %s", filepath.Base(file))
					continue
				}
				log.Printf("Rebuilt: %s", filepath.Base(file))
				copyFile(file, dst)
				if err := convertFile(dest); err != nil {
					log.Println(err)
				}
			}
			pending = make(map[string]fsnotify.Op)
		}
	}
}

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
	_ = os.Remove(dest)
	return nil
}

func convertFiles(path string, entry fs.DirEntry, err error) error {
	if err != nil {
		return err
	}
	if entry.IsDir() {
		return nil
	}
	return convertFile(path)
}

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
