package main

import (
	"encoding/json"
	"errors"
	"fmt"
	"net/http"
	"strings"
	"sync"
	"time"
)

// Project is the top-level workload container for a team or service.
type Project struct {
	ID          string `json:"id"`
	Name        string `json:"name"`
	Description string `json:"description"`
	CreatedAt   time.Time `json:"created_at"`
}

// Environment models a test target environment.
type Environment struct {
	ID        string `json:"id"`
	ProjectID string `json:"project_id"`
	Name      string `json:"name"`
	Type      string `json:"type"`
	CreatedAt time.Time `json:"created_at"`
}

// ScenarioDefinition captures a versioned runnable scenario.
type ScenarioDefinition struct {
	ID        string `json:"id"`
	ProjectID string `json:"project_id"`
	Name      string `json:"name"`
	Version   string `json:"version"`
	YAML      string `json:"yaml"`
	CreatedAt time.Time `json:"created_at"`
}

// ResultSummary stores the measurable outcome of a run.
type ResultSummary struct {
	TotalRequests      int64   `json:"total_requests"`
	SuccessfulRequests int64   `json:"successful_requests"`
	FailedRequests     int64   `json:"failed_requests"`
	ErrorRatePct       float64 `json:"error_rate_pct"`
	ThroughputRPS      float64 `json:"throughput_rps"`
	P95LatencyMs       float64 `json:"p95_latency_ms"`
	P99LatencyMs       float64 `json:"p99_latency_ms"`
}

// Run describes an execution of a scenario against an environment.
type Run struct {
	ID            string         `json:"id"`
	ProjectID     string         `json:"project_id"`
	EnvironmentID string         `json:"environment_id"`
	ScenarioID    string         `json:"scenario_id"`
	Status        string         `json:"status"`
	StartedAt     time.Time      `json:"started_at"`
	FinishedAt    time.Time      `json:"finished_at,omitempty"`
	Result        *ResultSummary `json:"result,omitempty"`
}

// CompareReport compares a current run to a baseline run.
type CompareReport struct {
	BaselineRunID string  `json:"baseline_run_id"`
	CurrentRunID  string  `json:"current_run_id"`
	DeltaP99Ms    float64 `json:"delta_p99_ms"`
	DeltaErrorPct float64 `json:"delta_error_pct"`
	Passed        bool    `json:"passed"`
}

// Store is a minimal in-memory product data model for the first product layer.
type Store struct {
	mu        sync.RWMutex
	projects  map[string]*Project
	envs      map[string]*Environment
	scenarios map[string]*ScenarioDefinition
	runs      map[string]*Run
}

func newStore() *Store {
	return &Store{
		projects:  make(map[string]*Project),
		envs:      make(map[string]*Environment),
		scenarios: make(map[string]*ScenarioDefinition),
		runs:      make(map[string]*Run),
	}
}

func (s *Store) createProject(name, description string) *Project {
	s.ID := fmt.Sprintf("proj_%d", time.Now().UnixNano())
	p := &Project{ID: s.ID, Name: name, Description: description, CreatedAt: time.Now().UTC()}
	s.mu.Lock()
	defer s.mu.Unlock()
	s.projects[p.ID] = p
	return p
}

func (s *Store) listProjects() []*Project {
	s.mu.RLock()
	defer s.mu.RUnlock()
	items := make([]*Project, 0, len(s.projects))
	for _, v := range s.projects {
		items = append(items, v)
	}
	return items
}

func (s *Store) createEnvironment(projectID, name, envType string) *Environment {
	id := fmt.Sprintf("env_%d", time.Now().UnixNano())
	e := &Environment{ID: id, ProjectID: projectID, Name: name, Type: envType, CreatedAt: time.Now().UTC()}
	s.mu.Lock()
	defer s.mu.Unlock()
	s.envs[e.ID] = e
	return e
}

func (s *Store) createScenario(projectID, name, version, yaml string) *ScenarioDefinition {
	id := fmt.Sprintf("scn_%d", time.Now().UnixNano())
	sc := &ScenarioDefinition{ID: id, ProjectID: projectID, Name: name, Version: version, YAML: yaml, CreatedAt: time.Now().UTC()}
	s.mu.Lock()
	defer s.mu.Unlock()
	s.scenarios[sc.ID] = sc
	return sc
}

func (s *Store) createRun(projectID, envID, scenarioID string) *Run {
	id := fmt.Sprintf("run_%d", time.Now().UnixNano())
	r := &Run{ID: id, ProjectID: projectID, EnvironmentID: envID, ScenarioID: scenarioID, Status: "queued", StartedAt: time.Now().UTC()}
	s.mu.Lock()
	defer s.mu.Unlock()
	s.runs[r.ID] = r
	return r
}

func (s *Store) updateRunStatus(runID, status string, result *ResultSummary) error {
	s.mu.Lock()
	defer s.mu.Unlock()
	r, ok := s.runs[runID]
	if !ok {
		return errors.New("run not found")
	}
	r.Status = status
	if result != nil {
		r.Result = result
	}
	if status == "succeeded" || status == "failed" || status == "aborted" {
		r.FinishedAt = time.Now().UTC()
	}
	return nil
}

func (s *Store) getRun(runID string) (*Run, bool) {
	s.mu.RLock()
	defer s.mu.RUnlock()
	r, ok := s.runs[runID]
	return r, ok
}

func (s *Store) listRuns() []*Run {
	s.mu.RLock()
	defer s.mu.RUnlock()
	items := make([]*Run, 0, len(s.runs))
	for _, v := range s.runs {
		items = append(items, v)
	}
	return items
}

func (s *Store) compareRuns(baselineID, currentID string) *CompareReport {
	baseline, ok1 := s.getRun(baselineID)
	current, ok2 := s.getRun(currentID)
	if !ok1 || !ok2 {
		return nil
	}
	if baseline.Result == nil || current.Result == nil {
		return nil
	}
	passed := current.Result.ErrorRatePct <= baseline.Result.ErrorRatePct && current.Result.P99LatencyMs <= baseline.Result.P99LatencyMs*1.10
	return &CompareReport{
		BaselineRunID: baselineID,
		CurrentRunID:  currentID,
		DeltaP99Ms:    current.Result.P99LatencyMs - baseline.Result.P99LatencyMs,
		DeltaErrorPct: current.Result.ErrorRatePct - baseline.Result.ErrorRatePct,
		Passed:        passed,
	}
}

func writeJSON(w http.ResponseWriter, code int, v any) {
	w.Header().Set("Content-Type", "application/json")
	w.WriteHeader(code)
	_ = json.NewEncoder(w).Encode(v)
}

func parseJSONBody(r *http.Request, v any) error {
	decoder := json.NewDecoder(r.Body)
	decoder.DisallowUnknownFields()
	return decoder.Decode(v)
}

func main() {
	store := newStore()

	// create a bootstrap project to make the API immediately useful
	bootProject := store.createProject("demo-project", "Bootstrap project for platform workflows")
	bootEnv := store.createEnvironment(bootProject.ID, "staging", "staging")
	bootScenario := store.createScenario(bootProject.ID, "checkout-smoke", "1.0", "version: \"1.0\"\n")
	store.createRun(bootProject.ID, bootEnv.ID, bootScenario.ID)

	http.HandleFunc("/health", func(w http.ResponseWriter, r *http.Request) {
		if r.Method != http.MethodGet {
			writeJSON(w, http.StatusMethodNotAllowed, map[string]string{"error": "method not allowed"})
			return
		}
		writeJSON(w, http.StatusOK, map[string]string{"status": "ok"})
	})

	http.HandleFunc("/projects", func(w http.ResponseWriter, r *http.Request) {
		switch r.Method {
		case http.MethodGet:
			writeJSON(w, http.StatusOK, store.listProjects())
		case http.MethodPost:
			var req struct {
				Name        string `json:"name"`
				Description string `json:"description"`
			}
			if err := parseJSONBody(r, &req); err != nil {
				writeJSON(w, http.StatusBadRequest, map[string]string{"error": err.Error()})
				return
			}
			if strings.TrimSpace(req.Name) == "" {
				writeJSON(w, http.StatusBadRequest, map[string]string{"error": "project name is required"})
				return
			}
			writeJSON(w, http.StatusCreated, store.createProject(req.Name, req.Description))
		default:
			writeJSON(w, http.StatusMethodNotAllowed, map[string]string{"error": "method not allowed"})
		}
	})

	http.HandleFunc("/environments", func(w http.ResponseWriter, r *http.Request) {
		if r.Method != http.MethodPost {
			writeJSON(w, http.StatusMethodNotAllowed, map[string]string{"error": "method not allowed"})
			return
		}
		var req struct {
			ProjectID string `json:"project_id"`
			Name      string `json:"name"`
			Type      string `json:"type"`
		}
		if err := parseJSONBody(r, &req); err != nil {
			writeJSON(w, http.StatusBadRequest, map[string]string{"error": err.Error()})
			return
		}
		if req.ProjectID == "" || strings.TrimSpace(req.Name) == "" {
			writeJSON(w, http.StatusBadRequest, map[string]string{"error": "project_id and name are required"})
			return
		}
		writeJSON(w, http.StatusCreated, store.createEnvironment(req.ProjectID, req.Name, req.Type))
	})

	http.HandleFunc("/scenarios", func(w http.ResponseWriter, r *http.Request) {
		if r.Method != http.MethodPost {
			writeJSON(w, http.StatusMethodNotAllowed, map[string]string{"error": "method not allowed"})
			return
		}
		var req struct {
			ProjectID string `json:"project_id"`
			Name      string `json:"name"`
			Version   string `json:"version"`
			YAML      string `json:"yaml"`
		}
		if err := parseJSONBody(r, &req); err != nil {
			writeJSON(w, http.StatusBadRequest, map[string]string{"error": err.Error()})
			return
		}
		if req.ProjectID == "" || strings.TrimSpace(req.Name) == "" {
			writeJSON(w, http.StatusBadRequest, map[string]string{"error": "project_id and name are required"})
			return
		}
		writeJSON(w, http.StatusCreated, store.createScenario(req.ProjectID, req.Name, req.Version, req.YAML))
	})

	http.HandleFunc("/runs", func(w http.ResponseWriter, r *http.Request) {
		switch r.Method {
		case http.MethodGet:
			writeJSON(w, http.StatusOK, store.listRuns())
		case http.MethodPost:
			var req struct {
				ProjectID     string `json:"project_id"`
				EnvironmentID string `json:"environment_id"`
				ScenarioID    string `json:"scenario_id"`
			}
			if err := parseJSONBody(r, &req); err != nil {
				writeJSON(w, http.StatusBadRequest, map[string]string{"error": err.Error()})
				return
			}
			if req.ProjectID == "" || req.EnvironmentID == "" || req.ScenarioID == "" {
				writeJSON(w, http.StatusBadRequest, map[string]string{"error": "project_id, environment_id, and scenario_id are required"})
				return
			}
			writeJSON(w, http.StatusCreated, store.createRun(req.ProjectID, req.EnvironmentID, req.ScenarioID))
		default:
			writeJSON(w, http.StatusMethodNotAllowed, map[string]string{"error": "method not allowed"})
		}
	})

	http.HandleFunc("/runs/", func(w http.ResponseWriter, r *http.Request) {
		if r.Method != http.MethodGet {
			writeJSON(w, http.StatusMethodNotAllowed, map[string]string{"error": "method not allowed"})
			return
		}
		path := strings.TrimPrefix(r.URL.Path, "/runs/")
		id := strings.TrimSpace(path)
		if id == "" {
			writeJSON(w, http.StatusNotFound, map[string]string{"error": "run id is required"})
			return
		}
		run, ok := store.getRun(id)
		if !ok {
			writeJSON(w, http.StatusNotFound, map[string]string{"error": "run not found"})
			return
		}
		writeJSON(w, http.StatusOK, run)
	})

	http.HandleFunc("/runs/compare", func(w http.ResponseWriter, r *http.Request) {
		if r.Method != http.MethodGet {
			writeJSON(w, http.StatusMethodNotAllowed, map[string]string{"error": "method not allowed"})
			return
		}
		baselineID := r.URL.Query().Get("baseline_id")
		currentID := r.URL.Query().Get("current_id")
		if baselineID == "" || currentID == "" {
			writeJSON(w, http.StatusBadRequest, map[string]string{"error": "baseline_id and current_id are required"})
			return
		}
		report := store.compareRuns(baselineID, currentID)
		if report == nil {
			writeJSON(w, http.StatusBadRequest, map[string]string{"error": "unable to compare runs; check both run ids and result availability"})
			return
		}
		writeJSON(w, http.StatusOK, report)
	})

	http.HandleFunc("/runs/status", func(w http.ResponseWriter, r *http.Request) {
		if r.Method != http.MethodPost {
			writeJSON(w, http.StatusMethodNotAllowed, map[string]string{"error": "method not allowed"})
			return
		}
		var req struct {
			RunID  string         `json:"run_id"`
			Status string         `json:"status"`
			Result *ResultSummary `json:"result,omitempty"`
		}
		if err := parseJSONBody(r, &req); err != nil {
			writeJSON(w, http.StatusBadRequest, map[string]string{"error": err.Error()})
			return
		}
		if req.RunID == "" || req.Status == "" {
			writeJSON(w, http.StatusBadRequest, map[string]string{"error": "run_id and status are required"})
			return
		}
		if err := store.updateRunStatus(req.RunID, req.Status, req.Result); err != nil {
			writeJSON(w, http.StatusNotFound, map[string]string{"error": err.Error()})
			return
		}
		writeJSON(w, http.StatusOK, map[string]string{"status": "updated"})
	})

	fmt.Println("volley control plane listening on :8080")
	if err := http.ListenAndServe(":8080", nil); err != nil {
		panic(err)
	}
}
