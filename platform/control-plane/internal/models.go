package internal

import "time"

// Project is the top-level workload container for a team or service.
type Project struct {
	ID          string    `json:"id"`
	Name        string    `json:"name"`
	Description string    `json:"description"`
	CreatedAt   time.Time `json:"created_at"`
}

// Environment models a test target environment.
type Environment struct {
	ID        string    `json:"id"`
	ProjectID string    `json:"project_id"`
	Name      string    `json:"name"`
	Type      string    `json:"type"`
	CreatedAt time.Time `json:"created_at"`
}

// ScenarioDefinition captures a versioned runnable scenario.
type ScenarioDefinition struct {
	ID        string    `json:"id"`
	ProjectID string    `json:"project_id"`
	Name      string    `json:"name"`
	Version   string    `json:"version"`
	YAML      string    `json:"yaml"`
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
	FinishedAt    *time.Time     `json:"finished_at,omitempty"`
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
