package internal

import (
	"fmt"
	"sync"
	"time"
)

// Worker represents a registered execution worker node.
type Worker struct {
	ID       string    `json:"id"`
	Name     string    `json:"name"`
	Status   string    `json:"status"`
	LastSeen time.Time `json:"last_seen"`
}

// WorkerRegistry tracks known workers and queued work.
type WorkerRegistry struct {
	mu     sync.RWMutex
	workers map[string]*Worker
	queue  []string
}

func NewWorkerRegistry() *WorkerRegistry {
	return &WorkerRegistry{workers: make(map[string]*Worker), queue: make([]string, 0)}
}

func (r *WorkerRegistry) Register(id, name string) *Worker {
	r.mu.Lock()
	defer r.mu.Unlock()
	w, exists := r.workers[id]
	if !exists {
		w = &Worker{ID: id, Name: name, Status: "online", LastSeen: time.Now().UTC()}
		r.workers[id] = w
		return w
	}
	w.Name = name
	w.Status = "online"
	w.LastSeen = time.Now().UTC()
	return w
}

func (r *WorkerRegistry) List() []*Worker {
	r.mu.RLock()
	defer r.mu.RUnlock()
	out := make([]*Worker, 0, len(r.workers))
	for _, w := range r.workers {
		copy := *w
		out = append(out, &copy)
	}
	return out
}

func (r *WorkerRegistry) Enqueue(runID string) bool {
	r.mu.Lock()
	defer r.mu.Unlock()
	if runID == "" {
		return false
	}
	for _, item := range r.queue {
		if item == runID {
			return true
		}
	}
	r.queue = append(r.queue, runID)
	return true
}

func (r *WorkerRegistry) Queue() []string {
	r.mu.RLock()
	defer r.mu.RUnlock()
	out := make([]string, len(r.queue))
	copy(out, r.queue)
	return out
}

func (r *WorkerRegistry) ClearQueued(runID string) {
	r.mu.Lock()
	defer r.mu.Unlock()
	filtered := make([]string, 0, len(r.queue))
	for _, item := range r.queue {
		if item != runID {
			filtered = append(filtered, item)
		}
	}
	r.queue = filtered
}

func (r *WorkerRegistry) DumpStatus() map[string]any {
	r.mu.RLock()
	defer r.mu.RUnlock()
	return map[string]any{
		"workers": len(r.workers),
		"queue":   r.queue,
		"status":  fmt.Sprintf("registered=%d queued=%d", len(r.workers), len(r.queue)),
	}
}
