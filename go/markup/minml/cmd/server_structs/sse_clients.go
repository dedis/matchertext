package server_structs

import "sync"

// SseClients tracks connected SSE clients for live reload notifications.
type SseClients struct {
	mu      sync.Mutex
	clients []chan string
}

// Add registers a new SSE client and returns its notification channel.
func (s *SseClients) Add() chan string {
	s.mu.Lock()
	defer s.mu.Unlock()
	ch := make(chan string, 1)
	s.clients = append(s.clients, ch)
	return ch
}

// Remove unregisters an SSE client.
func (s *SseClients) Remove(ch chan string) {
	s.mu.Lock()
	defer s.mu.Unlock()
	for i, c := range s.clients {
		if c == ch {
			s.clients = append(s.clients[:i], s.clients[i+1:]...)
			return
		}
	}
}

// Notify sends an event name to all connected SSE clients.
func (s *SseClients) Notify(event string) {
	s.mu.Lock()
	defer s.mu.Unlock()
	for _, ch := range s.clients {
		select {
		case ch <- event:
		default:
		}
	}
}
