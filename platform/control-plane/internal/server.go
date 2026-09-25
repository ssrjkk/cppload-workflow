package internal

import (
	"fmt"
	"net/http"
	"strings"
)

// Server exposes the product API surface for the control plane.
type Server struct {
	repo            Repository
	authConfig      AuthConfig
	workerRegistry  *WorkerRegistry
	projectMembers  map[string][]ProjectMember
	policies        map[string]*RunPolicy
}

func NewServer(repo Repository) *Server {
	return &Server{
		repo:           repo,
		authConfig:     NewAuthConfigFromEnv(),
		workerRegistry: NewWorkerRegistry(),
		projectMembers: make(map[string][]ProjectMember),
		policies:       make(map[string]*RunPolicy),
	}
}

func (s *Server) addMember(projectID, userID, role string) ProjectMember {
	member := ProjectMember{
		ProjectID: projectID,
		UserID:    userID,
		Role:      role,
		CreatedAt: time.Now().UTC(),
	}
	if s.projectMembers == nil {
		s.projectMembers = make(map[string][]ProjectMember)
	}
	s.projectMembers[projectID] = append(s.projectMembers[projectID], member)
	return member
}

func (s *Server) listMembers(projectID string) []ProjectMember {
	if s.projectMembers == nil {
		return nil
	}
	return s.projectMembers[projectID]
}

func (s *Server) addPolicy(projectID, name string, maxErrorRate float64, maxP99 float64) *RunPolicy {
	p := &RunPolicy{
		ID:              newPolicyID(),
		ProjectID:       projectID,
		Name:            name,
		MaxErrorRatePct: maxErrorRate,
		MaxP99LatencyMs: maxP99,
		Enabled:         true,
		CreatedAt:       time.Now().UTC(),
	}
	if s.policies == nil {
		s.policies = make(map[string]*RunPolicy)
	}
	s.policies[p.ID] = p
	return p
}

func (s *Server) listPolicies(projectID string) []*RunPolicy {
	if s.policies == nil {
		return nil
	}
	out := make([]*RunPolicy, 0)
	for _, p := range s.policies {
		if p.ProjectID == projectID {
			copy := *p
			out = append(out, &copy)
		}
	}
	return out
}

func (s *Server) evaluatePolicy(projectID string, result *ResultSummary) (bool, string) {
	if result == nil {
		return false, "missing result payload"
	}
	policies := s.listPolicies(projectID)
	if len(policies) == 0 {
		return true, "no policy defined"
	}
	for _, policy := range policies {
		if !policy.Enabled {
			continue
		}
		if result.ErrorRatePct > policy.MaxErrorRatePct {
			return false, fmt.Sprintf("error rate %.2f%% exceeds policy %.2f%%", result.ErrorRatePct, policy.MaxErrorRatePct)
		}
		if result.P99LatencyMs > policy.MaxP99LatencyMs {
			return false, fmt.Sprintf("p99 latency %.2fms exceeds policy %.2fms", result.P99LatencyMs, policy.MaxP99LatencyMs)
		}
	}
	return true, "policy satisfied"
}

func (s *Server) Handler() http.Handler {
	mux := http.NewServeMux()

	mux.HandleFunc("/health", func(w http.ResponseWriter, r *http.Request) {
		if r.Method != http.MethodGet {
			WriteJSON(w, http.StatusMethodNotAllowed, map[string]string{"error": "method not allowed"})
			return
		}
		WriteJSON(w, http.StatusOK, map[string]string{"status": "ok"})
	})

	mux.HandleFunc("/dashboard", func(w http.ResponseWriter, r *http.Request) {
		if r.Method != http.MethodGet {
			WriteJSON(w, http.StatusMethodNotAllowed, map[string]string{"error": "method not allowed"})
			return
		}
		projects, err := s.repo.ListProjects()
		if err != nil {
			WriteJSON(w, http.StatusInternalServerError, map[string]string{"error": err.Error()})
			return
		}
		runs, err := s.repo.ListRuns()
		if err != nil {
			WriteJSON(w, http.StatusInternalServerError, map[string]string{"error": err.Error()})
			return
		}
		if len(runs) == 0 {
			WriteJSON(w, http.StatusOK, map[string]any{
				"projects": len(projects),
				"runs":     0,
				"workers":  len(s.workerRegistry.List()),
				"queue":    s.workerRegistry.Queue(),
			})
			return
		}
		var failedCount int
		var totalRPS float64
		var totalP99 float64
		for _, run := range runs {
			if run.Result != nil {
				if run.Status == "failed" || run.Result.ErrorRatePct > 0 {
					failedCount++
				}
				totalRPS += run.Result.ThroughputRPS
				totalP99 += run.Result.P99LatencyMs
			}
		}
		WriteJSON(w, http.StatusOK, map[string]any{
			"projects": len(projects),
			"runs":     len(runs),
			"workers":  len(s.workerRegistry.List()),
			"queue":    s.workerRegistry.Queue(),
			"failed_runs": failedCount,
			"avg_rps": func() float64 {
				if len(runs) == 0 {
					return 0
				}
				return totalRPS / float64(len(runs))
			}(),
			"avg_p99_ms": func() float64 {
				if len(runs) == 0 {
					return 0
				}
				return totalP99 / float64(len(runs))
			}(),
		})
	})

	mux.HandleFunc("/projects", func(w http.ResponseWriter, r *http.Request) {
		switch r.Method {
		case http.MethodGet:
			projects, err := s.repo.ListProjects()
			if err != nil {
				WriteJSON(w, http.StatusInternalServerError, map[string]string{"error": err.Error()})
				return
			}
			WriteJSON(w, http.StatusOK, projects)
		case http.MethodPost:
			var req struct {
				Name        string `json:"name"`
				Description string `json:"description"`
			}
			if err := ParseJSONBody(r, &req); err != nil {
				WriteJSON(w, http.StatusBadRequest, map[string]string{"error": err.Error()})
				return
			}
			if !ValidateName(req.Name) {
				WriteJSON(w, http.StatusBadRequest, map[string]string{"error": "project name is required"})
				return
			}
			project, err := s.repo.CreateProject(req.Name, req.Description)
			if err != nil {
				WriteJSON(w, http.StatusInternalServerError, map[string]string{"error": err.Error()})
				return
			}
			WriteJSON(w, http.StatusCreated, project)
		default:
			WriteJSON(w, http.StatusMethodNotAllowed, map[string]string{"error": "method not allowed"})
		}
	})

	mux.HandleFunc("/projects/", func(w http.ResponseWriter, r *http.Request) {
		if !strings.HasSuffix(r.URL.Path, "/members") {
			WriteJSON(w, http.StatusNotFound, map[string]string{"error": "not found"})
			return
		}
		if r.Method != http.MethodPost {
			WriteJSON(w, http.StatusMethodNotAllowed, map[string]string{"error": "method not allowed"})
			return
		}
		projectID := strings.TrimSuffix(strings.TrimPrefix(r.URL.Path, "/projects/"), "/members")
		if projectID == "" {
			WriteJSON(w, http.StatusBadRequest, map[string]string{"error": "project_id is required"})
			return
		}
		var req struct {
			UserID string `json:"user_id"`
			Role   string `json:"role"`
		}
		if err := ParseJSONBody(r, &req); err != nil {
			WriteJSON(w, http.StatusBadRequest, map[string]string{"error": err.Error()})
			return
		}
		if !ValidateName(req.UserID) || !ValidateName(req.Role) {
			WriteJSON(w, http.StatusBadRequest, map[string]string{"error": "user_id and role are required"})
			return
		}
		member := s.addMember(projectID, req.UserID, req.Role)
		WriteJSON(w, http.StatusCreated, member)
	})

	mux.HandleFunc("/policies", func(w http.ResponseWriter, r *http.Request) {
		switch r.Method {
		case http.MethodGet:
			projectID := r.URL.Query().Get("project_id")
			WriteJSON(w, http.StatusOK, s.listPolicies(projectID))
		case http.MethodPost:
			var req struct {
				ProjectID       string  `json:"project_id"`
				Name            string  `json:"name"`
				MaxErrorRatePct float64 `json:"max_error_rate_pct"`
				MaxP99LatencyMs float64 `json:"max_p99_latency_ms"`
			}
			if err := ParseJSONBody(r, &req); err != nil {
				WriteJSON(w, http.StatusBadRequest, map[string]string{"error": err.Error()})
				return
			}
			if !ValidateName(req.ProjectID) || !ValidateName(req.Name) {
				WriteJSON(w, http.StatusBadRequest, map[string]string{"error": "project_id and name are required"})
				return
			}
			policy := s.addPolicy(req.ProjectID, req.Name, req.MaxErrorRatePct, req.MaxP99LatencyMs)
			WriteJSON(w, http.StatusCreated, policy)
		default:
			WriteJSON(w, http.StatusMethodNotAllowed, map[string]string{"error": "method not allowed"})
		}
	})

	mux.HandleFunc("/environments", func(w http.ResponseWriter, r *http.Request) {
		if r.Method != http.MethodPost {
			WriteJSON(w, http.StatusMethodNotAllowed, map[string]string{"error": "method not allowed"})
			return
		}
		var req struct {
			ProjectID string `json:"project_id"`
			Name      string `json:"name"`
			Type      string `json:"type"`
		}
		if err := ParseJSONBody(r, &req); err != nil {
			WriteJSON(w, http.StatusBadRequest, map[string]string{"error": err.Error()})
			return
		}
		if req.ProjectID == "" || !ValidateName(req.Name) {
			WriteJSON(w, http.StatusBadRequest, map[string]string{"error": "project_id and name are required"})
			return
		}
		env, err := s.repo.CreateEnvironment(req.ProjectID, req.Name, req.Type)
		if err != nil {
			WriteJSON(w, http.StatusInternalServerError, map[string]string{"error": err.Error()})
			return
		}
		WriteJSON(w, http.StatusCreated, env)
	})

	mux.HandleFunc("/scenarios", func(w http.ResponseWriter, r *http.Request) {
		if r.Method != http.MethodPost {
			WriteJSON(w, http.StatusMethodNotAllowed, map[string]string{"error": "method not allowed"})
			return
		}
		var req struct {
			ProjectID string `json:"project_id"`
			Name      string `json:"name"`
			Version   string `json:"version"`
			YAML      string `json:"yaml"`
		}
		if err := ParseJSONBody(r, &req); err != nil {
			WriteJSON(w, http.StatusBadRequest, map[string]string{"error": err.Error()})
			return
		}
		if req.ProjectID == "" || !ValidateName(req.Name) {
			WriteJSON(w, http.StatusBadRequest, map[string]string{"error": "project_id and name are required"})
			return
		}
		scenario, err := s.repo.CreateScenario(req.ProjectID, req.Name, req.Version, req.YAML)
		if err != nil {
			WriteJSON(w, http.StatusInternalServerError, map[string]string{"error": err.Error()})
			return
		}
		WriteJSON(w, http.StatusCreated, scenario)
	})

	mux.HandleFunc("/workers", func(w http.ResponseWriter, r *http.Request) {
		switch r.Method {
		case http.MethodGet:
			WriteJSON(w, http.StatusOK, s.workerRegistry.List())
		case http.MethodPost:
			var req struct {
				ID   string `json:"id"`
				Name string `json:"name"`
			}
			if err := ParseJSONBody(r, &req); err != nil {
				WriteJSON(w, http.StatusBadRequest, map[string]string{"error": err.Error()})
				return
			}
			if req.ID == "" {
				WriteJSON(w, http.StatusBadRequest, map[string]string{"error": "worker id is required"})
				return
			}
			WriteJSON(w, http.StatusCreated, s.workerRegistry.Register(req.ID, req.Name))
		default:
			WriteJSON(w, http.StatusMethodNotAllowed, map[string]string{"error": "method not allowed"})
		}
	})

	mux.HandleFunc("/workers/heartbeat", func(w http.ResponseWriter, r *http.Request) {
		if r.Method != http.MethodPost {
			WriteJSON(w, http.StatusMethodNotAllowed, map[string]string{"error": "method not allowed"})
			return
		}
		var req struct {
			ID   string `json:"id"`
			Name string `json:"name"`
		}
		if err := ParseJSONBody(r, &req); err != nil {
			WriteJSON(w, http.StatusBadRequest, map[string]string{"error": err.Error()})
			return
		}
		if req.ID == "" {
			WriteJSON(w, http.StatusBadRequest, map[string]string{"error": "worker id is required"})
			return
		}
		worker := s.workerRegistry.Register(req.ID, req.Name)
		assignedRunID, hasAssignment := s.workerRegistry.Heartbeat(req.ID)
		WriteJSON(w, http.StatusOK, map[string]any{
			"worker":          worker,
			"assigned_run_id": assignedRunID,
			"has_assignment":  hasAssignment,
		})
	})

	mux.HandleFunc("/runs", func(w http.ResponseWriter, r *http.Request) {
		switch r.Method {
		case http.MethodGet:
			runs, err := s.repo.ListRuns()
			if err != nil {
				WriteJSON(w, http.StatusInternalServerError, map[string]string{"error": err.Error()})
				return
			}
			WriteJSON(w, http.StatusOK, runs)
		case http.MethodPost:
			var req struct {
				ProjectID     string `json:"project_id"`
				EnvironmentID string `json:"environment_id"`
				ScenarioID    string `json:"scenario_id"`
			}
			if err := ParseJSONBody(r, &req); err != nil {
				WriteJSON(w, http.StatusBadRequest, map[string]string{"error": err.Error()})
				return
			}
			if req.ProjectID == "" || req.EnvironmentID == "" || req.ScenarioID == "" {
				WriteJSON(w, http.StatusBadRequest, map[string]string{"error": "project_id, environment_id and scenario_id are required"})
				return
			}
			run, err := s.repo.CreateRun(req.ProjectID, req.EnvironmentID, req.ScenarioID)
			if err != nil {
				WriteJSON(w, http.StatusInternalServerError, map[string]string{"error": err.Error()})
				return
			}
			s.workerRegistry.Enqueue(run.ID)
			WriteJSON(w, http.StatusCreated, run)
		default:
			WriteJSON(w, http.StatusMethodNotAllowed, map[string]string{"error": "method not allowed"})
		}
	})

	mux.HandleFunc("/runs/queue", func(w http.ResponseWriter, r *http.Request) {
		if r.Method != http.MethodPost {
			WriteJSON(w, http.StatusMethodNotAllowed, map[string]string{"error": "method not allowed"})
			return
		}
		var req struct {
			ProjectID     string `json:"project_id"`
			EnvironmentID string `json:"environment_id"`
			ScenarioID    string `json:"scenario_id"`
		}
		if err := ParseJSONBody(r, &req); err != nil {
			WriteJSON(w, http.StatusBadRequest, map[string]string{"error": err.Error()})
			return
		}
		if req.ProjectID == "" || req.EnvironmentID == "" || req.ScenarioID == "" {
			WriteJSON(w, http.StatusBadRequest, map[string]string{"error": "project_id, environment_id and scenario_id are required"})
			return
		}
		run, err := s.repo.CreateRun(req.ProjectID, req.EnvironmentID, req.ScenarioID)
		if err != nil {
			WriteJSON(w, http.StatusInternalServerError, map[string]string{"error": err.Error()})
			return
		}
		s.workerRegistry.Enqueue(run.ID)
		WriteJSON(w, http.StatusAccepted, map[string]string{"run_id": run.ID, "status": "queued"})
	})

	mux.HandleFunc("/runs/evaluate", func(w http.ResponseWriter, r *http.Request) {
		if r.Method != http.MethodPost {
			WriteJSON(w, http.StatusMethodNotAllowed, map[string]string{"error": "method not allowed"})
			return
		}
		var req struct {
			ProjectID string         `json:"project_id"`
			RunID     string         `json:"run_id"`
			Result    *ResultSummary `json:"result"`
		}
		if err := ParseJSONBody(r, &req); err != nil {
			WriteJSON(w, http.StatusBadRequest, map[string]string{"error": err.Error()})
			return
		}
		if req.ProjectID == "" || req.RunID == "" || req.Result == nil {
			WriteJSON(w, http.StatusBadRequest, map[string]string{"error": "project_id, run_id and result are required"})
			return
		}
		passed, reason := s.evaluatePolicy(req.ProjectID, req.Result)
		WriteJSON(w, http.StatusOK, map[string]any{
			"run_id": req.RunID,
			"passed": passed,
			"reason": reason,
		})
	})

	mux.HandleFunc("/runs/", func(w http.ResponseWriter, r *http.Request) {
		if r.Method != http.MethodGet {
			WriteJSON(w, http.StatusMethodNotAllowed, map[string]string{"error": "method not allowed"})
			return
		}
		id := strings.TrimPrefix(r.URL.Path, "/runs/")
		if id == "" {
			WriteJSON(w, http.StatusBadRequest, map[string]string{"error": "run id is required"})
			return
		}
		run, ok, err := s.repo.GetRun(id)
		if err != nil {
			WriteJSON(w, http.StatusInternalServerError, map[string]string{"error": err.Error()})
			return
		}
		if !ok {
			WriteJSON(w, http.StatusNotFound, map[string]string{"error": "run not found"})
			return
		}
		WriteJSON(w, http.StatusOK, run)
	})

	mux.HandleFunc("/runs/compare", func(w http.ResponseWriter, r *http.Request) {
		if r.Method != http.MethodGet {
			WriteJSON(w, http.StatusMethodNotAllowed, map[string]string{"error": "method not allowed"})
			return
		}
		baselineID := r.URL.Query().Get("baseline_id")
		currentID := r.URL.Query().Get("current_id")
		if baselineID == "" || currentID == "" {
			WriteJSON(w, http.StatusBadRequest, map[string]string{"error": "baseline_id and current_id are required"})
			return
		}
		report, err := s.repo.CompareRuns(baselineID, currentID)
		if err != nil {
			WriteJSON(w, http.StatusBadRequest, map[string]string{"error": err.Error()})
			return
		}
		WriteJSON(w, http.StatusOK, report)
	})

	mux.HandleFunc("/runs/status", func(w http.ResponseWriter, r *http.Request) {
		if r.Method != http.MethodPost {
			WriteJSON(w, http.StatusMethodNotAllowed, map[string]string{"error": "method not allowed"})
			return
		}
		var req struct {
			RunID  string         `json:"run_id"`
			Status string         `json:"status"`
			Result *ResultSummary `json:"result,omitempty"`
		}
		if err := ParseJSONBody(r, &req); err != nil {
			WriteJSON(w, http.StatusBadRequest, map[string]string{"error": err.Error()})
			return
		}
		if req.RunID == "" || req.Status == "" {
			WriteJSON(w, http.StatusBadRequest, map[string]string{"error": "run_id and status are required"})
			return
		}
		if err := s.repo.UpdateRunStatus(req.RunID, req.Status, req.Result); err != nil {
			WriteJSON(w, http.StatusNotFound, map[string]string{"error": err.Error()})
			return
		}
		if req.Result != nil {
			s.workerRegistry.ClearQueued(req.RunID)
		}
		WriteJSON(w, http.StatusOK, map[string]string{"status": "updated"})
	})

	return s.authConfig.Middleware(mux)
}

// compile guard: keep this file package internal only.
