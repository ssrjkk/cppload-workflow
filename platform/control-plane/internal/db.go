package internal

import (
	"database/sql"
	"encoding/json"
	"errors"
	"fmt"
	"net/http"
	"os"
	"strings"
	"time"

	_ "github.com/lib/pq"
)

// Repository is the data access contract for product-level state.
type Repository interface {
	CreateProject(name, description string) (*Project, error)
	ListProjects() ([]*Project, error)
	CreateEnvironment(projectID, name, envType string) (*Environment, error)
	CreateScenario(projectID, name, version, yaml string) (*ScenarioDefinition, error)
	CreateRun(projectID, envID, scenarioID string) (*Run, error)
	GetRun(runID string) (*Run, bool, error)
	ListRuns() ([]*Run, error)
	UpdateRunStatus(runID, status string, result *ResultSummary) error
	CompareRuns(baselineID, currentID string) (*CompareReport, error)
	Close() error
}

// DBConfig holds the Postgres connection assumptions.
type DBConfig struct {
	DSN string
}

// NewDBConfigFromEnv builds the DB config from environment variables.
func NewDBConfigFromEnv() DBConfig {
	dsn := os.Getenv("VOLLEY_DB_DSN")
	if dsn == "" {
		dsn = "postgres://postgres:postgres@localhost:5432/volley?sslmode=disable"
	}
	return DBConfig{DSN: dsn}
}

// PostgresRepository provides a database-backed implementation of the product state.
type PostgresRepository struct {
	db *sql.DB
}

// NewRepositoryFromEnv constructs a repository and initializes the schema.
func NewRepositoryFromEnv() (*PostgresRepository, error) {
	cfg := NewDBConfigFromEnv()
	db, err := sql.Open("postgres", cfg.DSN)
	if err != nil {
		return nil, err
	}
	if err := db.Ping(); err != nil {
		_ = db.Close()
		return nil, err
	}

	repo := &PostgresRepository{db: db}
	if err := repo.ensureSchema(); err != nil {
		_ = db.Close()
		return nil, err
	}
	return repo, nil
}

func (r *PostgresRepository) Close() error {
	if r == nil || r.db == nil {
		return nil
	}
	return r.db.Close()
}

func (r *PostgresRepository) ensureSchema() error {
	queries := []string{
		`CREATE TABLE IF NOT EXISTS projects (
			id TEXT PRIMARY KEY,
			name TEXT NOT NULL,
			description TEXT,
			created_at TIMESTAMPTZ NOT NULL DEFAULT NOW()
		)`,
		`CREATE TABLE IF NOT EXISTS environments (
			id TEXT PRIMARY KEY,
			project_id TEXT NOT NULL REFERENCES projects(id) ON DELETE CASCADE,
			name TEXT NOT NULL,
			type TEXT NOT NULL,
			created_at TIMESTAMPTZ NOT NULL DEFAULT NOW()
		)`,
		`CREATE TABLE IF NOT EXISTS scenarios (
			id TEXT PRIMARY KEY,
			project_id TEXT NOT NULL REFERENCES projects(id) ON DELETE CASCADE,
			name TEXT NOT NULL,
			version TEXT NOT NULL,
			yaml TEXT NOT NULL,
			created_at TIMESTAMPTZ NOT NULL DEFAULT NOW()
		)`,
		`CREATE TABLE IF NOT EXISTS runs (
			id TEXT PRIMARY KEY,
			project_id TEXT NOT NULL REFERENCES projects(id) ON DELETE CASCADE,
			environment_id TEXT NOT NULL REFERENCES environments(id) ON DELETE CASCADE,
			scenario_id TEXT NOT NULL REFERENCES scenarios(id) ON DELETE CASCADE,
			status TEXT NOT NULL,
			started_at TIMESTAMPTZ NOT NULL,
			finished_at TIMESTAMPTZ,
			total_requests BIGINT DEFAULT 0,
			successful_requests BIGINT DEFAULT 0,
			failed_requests BIGINT DEFAULT 0,
			error_rate_pct DOUBLE PRECISION DEFAULT 0,
			throughput_rps DOUBLE PRECISION DEFAULT 0,
			p95_latency_ms DOUBLE PRECISION DEFAULT 0,
			p99_latency_ms DOUBLE PRECISION DEFAULT 0
		)`,
	}
	for _, q := range queries {
		if _, err := r.db.Exec(q); err != nil {
			return err
		}
	}
	return nil
}

func (r *PostgresRepository) CreateProject(name, description string) (*Project, error) {
	id := fmt.Sprintf("proj_%d", time.Now().UnixNano())
	project := &Project{ID: id, Name: name, Description: description, CreatedAt: time.Now().UTC()}
	_, err := r.db.Exec(
		`INSERT INTO projects (id, name, description, created_at) VALUES ($1, $2, $3, $4)`,
		project.ID, project.Name, project.Description, project.CreatedAt,
	)
	if err != nil {
		return nil, err
	}
	return project, nil
}

func (r *PostgresRepository) ListProjects() ([]*Project, error) {
	rows, err := r.db.Query(`SELECT id, name, description, created_at FROM projects ORDER BY created_at DESC`)
	if err != nil {
		return nil, err
	}
	defer rows.Close()

	projects := make([]*Project, 0)
	for rows.Next() {
		p := &Project{}
		if err := rows.Scan(&p.ID, &p.Name, &p.Description, &p.CreatedAt); err != nil {
			return nil, err
		}
		projects = append(projects, p)
	}
	return projects, rows.Err()
}

func (r *PostgresRepository) CreateEnvironment(projectID, name, envType string) (*Environment, error) {
	id := fmt.Sprintf("env_%d", time.Now().UnixNano())
	env := &Environment{ID: id, ProjectID: projectID, Name: name, Type: envType, CreatedAt: time.Now().UTC()}
	_, err := r.db.Exec(
		`INSERT INTO environments (id, project_id, name, type, created_at) VALUES ($1, $2, $3, $4, $5)`,
		env.ID, env.ProjectID, env.Name, env.Type, env.CreatedAt,
	)
	if err != nil {
		return nil, err
	}
	return env, nil
}

func (r *PostgresRepository) CreateScenario(projectID, name, version, yaml string) (*ScenarioDefinition, error) {
	id := fmt.Sprintf("scn_%d", time.Now().UnixNano())
	scenario := &ScenarioDefinition{ID: id, ProjectID: projectID, Name: name, Version: version, YAML: yaml, CreatedAt: time.Now().UTC()}
	_, err := r.db.Exec(
		`INSERT INTO scenarios (id, project_id, name, version, yaml, created_at) VALUES ($1, $2, $3, $4, $5, $6)`,
		scenario.ID, scenario.ProjectID, scenario.Name, scenario.Version, scenario.YAML, scenario.CreatedAt,
	)
	if err != nil {
		return nil, err
	}
	return scenario, nil
}

func (r *PostgresRepository) CreateRun(projectID, envID, scenarioID string) (*Run, error) {
	id := fmt.Sprintf("run_%d", time.Now().UnixNano())
	run := &Run{ID: id, ProjectID: projectID, EnvironmentID: envID, ScenarioID: scenarioID, Status: "queued", StartedAt: time.Now().UTC()}
	_, err := r.db.Exec(
		`INSERT INTO runs (id, project_id, environment_id, scenario_id, status, started_at) VALUES ($1, $2, $3, $4, $5, $6)`,
		run.ID, run.ProjectID, run.EnvironmentID, run.ScenarioID, run.Status, run.StartedAt,
	)
	if err != nil {
		return nil, err
	}
	return run, nil
}

func (r *PostgresRepository) GetRun(runID string) (*Run, bool, error) {
	row := r.db.QueryRow(
		`SELECT id, project_id, environment_id, scenario_id, status, started_at, finished_at,
			total_requests, successful_requests, failed_requests, error_rate_pct, throughput_rps,
			p95_latency_ms, p99_latency_ms
		 FROM runs WHERE id = $1`, runID,
	)

	run := &Run{}
	var finishedAt sql.NullTime
	var totalReq, successReq, failedReq sql.NullInt64
	var errorRate, throughput, p95, p99 sql.NullFloat64
	if err := row.Scan(
		&run.ID, &run.ProjectID, &run.EnvironmentID, &run.ScenarioID,
		&run.Status, &run.StartedAt, &finishedAt,
		&totalReq, &successReq, &failedReq, &errorRate, &throughput, &p95, &p99,
	); err != nil {
		if errors.Is(err, sql.ErrNoRows) {
			return nil, false, nil
		}
		return nil, false, err
	}
	if finishedAt.Valid {
		run.FinishedAt = &finishedAt.Time
	}
	if totalReq.Valid || successReq.Valid || failedReq.Valid || errorRate.Valid || throughput.Valid || p95.Valid || p99.Valid {
		run.Result = &ResultSummary{
			TotalRequests:      int64Val(totalReq),
			SuccessfulRequests: int64Val(successReq),
			FailedRequests:     int64Val(failedReq),
			ErrorRatePct:       float64Val(errorRate),
			ThroughputRPS:      float64Val(throughput),
			P95LatencyMs:       float64Val(p95),
			P99LatencyMs:       float64Val(p99),
		}
	}
	return run, true, nil
}

func (r *PostgresRepository) ListRuns() ([]*Run, error) {
	rows, err := r.db.Query(
		`SELECT id, project_id, environment_id, scenario_id, status, started_at, finished_at,
			total_requests, successful_requests, failed_requests, error_rate_pct, throughput_rps,
			p95_latency_ms, p99_latency_ms
		 FROM runs ORDER BY started_at DESC`)
	if err != nil {
		return nil, err
	}
	defer rows.Close()

	result := make([]*Run, 0)
	for rows.Next() {
		run := &Run{}
		var finishedAt sql.NullTime
		var totalReq, successReq, failedReq sql.NullInt64
		var errorRate, throughput, p95, p99 sql.NullFloat64
		if err := rows.Scan(
			&run.ID, &run.ProjectID, &run.EnvironmentID, &run.ScenarioID,
			&run.Status, &run.StartedAt, &finishedAt,
			&totalReq, &successReq, &failedReq, &errorRate, &throughput, &p95, &p99,
		); err != nil {
			return nil, err
		}
		if finishedAt.Valid {
			run.FinishedAt = &finishedAt.Time
		}
		if totalReq.Valid || successReq.Valid || failedReq.Valid || errorRate.Valid || throughput.Valid || p95.Valid || p99.Valid {
			run.Result = &ResultSummary{
				TotalRequests:      int64Val(totalReq),
				SuccessfulRequests: int64Val(successReq),
				FailedRequests:     int64Val(failedReq),
				ErrorRatePct:       float64Val(errorRate),
				ThroughputRPS:      float64Val(throughput),
				P95LatencyMs:       float64Val(p95),
				P99LatencyMs:       float64Val(p99),
			}
		}
		result = append(result, run)
	}
	return result, rows.Err()
}

func (r *PostgresRepository) UpdateRunStatus(runID, status string, result *ResultSummary) error {
	if result == nil {
		_, err := r.db.Exec(
			`UPDATE runs SET status = $1, finished_at = NOW() WHERE id = $2`,
			status, runID,
		)
		return err
	}
	_, err := r.db.Exec(
		`UPDATE runs SET status = $1,
			finished_at = CASE WHEN $1 IN ('succeeded','failed','aborted') THEN NOW() ELSE finished_at END,
			total_requests = $2,
			successful_requests = $3,
			failed_requests = $4,
			error_rate_pct = $5,
			throughput_rps = $6,
			p95_latency_ms = $7,
			p99_latency_ms = $8
		 WHERE id = $9`,
		status,
		result.TotalRequests,
		result.SuccessfulRequests,
		result.FailedRequests,
		result.ErrorRatePct,
		result.ThroughputRPS,
		result.P95LatencyMs,
		result.P99LatencyMs,
		runID,
	)
	return err
}

func (r *PostgresRepository) CompareRuns(baselineID, currentID string) (*CompareReport, error) {
	baseline, ok, err := r.GetRun(baselineID)
	if err != nil {
		return nil, err
	}
	if !ok || baseline.Result == nil {
		return nil, errors.New("baseline run missing or missing result")
	}
	current, ok, err := r.GetRun(currentID)
	if err != nil {
		return nil, err
	}
	if !ok || current.Result == nil {
		return nil, errors.New("current run missing or missing result")
	}
	passed := current.Result.ErrorRatePct <= baseline.Result.ErrorRatePct && current.Result.P99LatencyMs <= baseline.Result.P99LatencyMs*1.10
	return &CompareReport{
		BaselineRunID: baselineID,
		CurrentRunID:  currentID,
		DeltaP99Ms:    current.Result.P99LatencyMs - baseline.Result.P99LatencyMs,
		DeltaErrorPct: current.Result.ErrorRatePct - baseline.Result.ErrorRatePct,
		Passed:        passed,
	}, nil
}

func int64Val(v sql.NullInt64) int64 {
	if v.Valid {
		return v.Int64
	}
	return 0
}

func float64Val(v sql.NullFloat64) float64 {
	if v.Valid {
		return v.Float64
	}
	return 0
}

// Environment variable helper for quick local setup.
func EnvOrDefault(key, fallback string) string {
	if value := os.Getenv(key); value != "" {
		return value
	}
	return fallback
}

// Json helpers used by API handlers.
func WriteJSON(w http.ResponseWriter, code int, value any) {
	w.Header().Set("Content-Type", "application/json")
	w.WriteHeader(code)
	_ = json.NewEncoder(w).Encode(value)
}

func ParseJSONBody(r *http.Request, value any) error {
	decoder := json.NewDecoder(r.Body)
	decoder.DisallowUnknownFields()
	return decoder.Decode(value)
}

func ValidateName(name string) bool {
	return strings.TrimSpace(name) != ""
}
