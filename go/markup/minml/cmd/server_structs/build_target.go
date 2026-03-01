package server_structs

import (
	"io/fs"
	"os"
	"path/filepath"

	"github.com/spf13/afero"
)

// BuildTarget abstracts where the server stores converted build artifacts.
type BuildTarget interface {
	Init() error
	Cleanup() error

	FS() fs.FS
	WriteFile(path string, data []byte) error
	RemoveFile(path string) error
}

// DiskTarget stores build artifacts inside a directory on disk.
type DiskTarget struct {
	root string
}

// NewDiskTarget creates a disk-backed build target rooted at path.
func NewDiskTarget(path string) *DiskTarget {
	return &DiskTarget{root: path}
}

// Init creates the target root directory.
func (d *DiskTarget) Init() error {
	return os.MkdirAll(d.root, 0755)
}

// Cleanup removes all files created for the build target.
func (d *DiskTarget) Cleanup() error {
	return os.RemoveAll(d.root)
}

// FS returns a read-only filesystem view rooted at the disk target.
func (d *DiskTarget) FS() fs.FS {
	return os.DirFS(d.root)
}

// WriteFile writes data to a relative file path in the disk target.
func (d *DiskTarget) WriteFile(path string, data []byte) error {
	full := filepath.Join(d.root, path)
	if err := os.MkdirAll(filepath.Dir(full), 0755); err != nil {
		return err
	}
	return os.WriteFile(full, data, 0644)
}

// RemoveFile deletes a relative file path from the disk target.
func (d *DiskTarget) RemoveFile(path string) error {
	return os.Remove(filepath.Join(d.root, path))
}

// MemoryTarget stores build artifacts in an in-memory filesystem.
type MemoryTarget struct {
	fs afero.Fs
}

// NewMemoryTarget creates an in-memory build target.
func NewMemoryTarget() *MemoryTarget {
	return &MemoryTarget{fs: afero.NewMemMapFs()}
}

// Init is a no-op for the in-memory target.
func (m *MemoryTarget) Init() error { return nil }

// Cleanup is a no-op for the in-memory target.
func (m *MemoryTarget) Cleanup() error { return nil }

// FS returns a filesystem view of the in-memory target.
func (m *MemoryTarget) FS() fs.FS {
	return afero.NewIOFS(m.fs)
}

// WriteFile writes data to a relative file path in memory.
func (m *MemoryTarget) WriteFile(path string, data []byte) error {
	return afero.WriteFile(m.fs, path, data, 0644)
}

// RemoveFile deletes a relative file path from memory.
func (m *MemoryTarget) RemoveFile(path string) error {
	return m.fs.Remove(path)
}
