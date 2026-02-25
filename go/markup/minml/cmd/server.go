package main

import (
	"io/fs"
	"log"
	"net/http"
	"os"
	"path/filepath"
)

func Server(path string, port string) {
	// Create a temporary build folder
	// The "__" prefix is to prevent potential clashing
	dir, err := os.MkdirTemp("", "__build")
	if err != nil {
		log.Fatal(err)
		return
	}
	defer os.RemoveAll(dir)

	fi, err := os.Stat(path)
	if err != nil {
		log.Fatal(err)
		return
	}

	// Copy all files from source dir to the new temp __build dir
	switch mode := fi.Mode(); {
	case mode.IsDir():
		err := os.CopyFS(dir, os.DirFS(path))
		if err != nil {
			log.Fatal(err)
			return
		}
	case mode.IsRegular():
		data, err := os.ReadFile(path)
		if err != nil {
			log.Fatal(err)
			return
		}
		dest := filepath.Join(dir, filepath.Base(path))
		if err := os.WriteFile(dest, data, 0644); err != nil {
			log.Fatal(err)
			return
		}
	}

	// Convert all .minml files to .html files
	err = filepath.WalkDir(dir, convertFiles)
	if err != nil {
		log.Fatal(err)
		return
	}

	// Serve the build directory
	log.Printf("Serving %s on http://localhost:%s", dir, port)
	http.Handle("/", http.FileServer(http.Dir(dir)))
	if err := http.ListenAndServe(":"+port, nil); err != nil {
		log.Fatal(err)
	}
}

func convertFiles(path string, entry fs.DirEntry, err error) error {
	if err != nil {
		return err
	}

	if !entry.IsDir() && filepath.Ext(entry.Name()) == ".minml" {
		out, err := os.Create(path[:len(path)-len(".minml")] + ".html")
		if err != nil {
			return err
		}
		defer out.Close()

		if err := Convert(path, out); err != nil {
			return err
		}

		_ = os.Remove(path)
	}

	return nil
}
