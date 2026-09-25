package internal

import "time"

// RunPolicy defines the threshold contract used by CI and release gates.
type RunPolicy struct {
	ID               string    `json:"id"`
	ProjectID        string    `json:"project_id"`
	Name             string    `json:"name"`
	MaxErrorRatePct  float64   `json:"max_error_rate_pct"`
	MaxP99LatencyMs  float64   `json:"max_p99_latency_ms"`
	Enabled          bool      `json:"enabled"`
	CreatedAt        time.Time `json:"created_at"`
}

// ProjectMember is the access-control entry for a user in a project.
type ProjectMember struct {
	ProjectID string `json:"project_id"`
	UserID    string `json:"user_id"`
	Role      string `json:"role"`
	CreatedAt time.Time `json:"created_at"`
}

func newPolicyID() string {
	return fmt.Sprintf("policy_%d", time.Now().UnixNano())
}

func newMemberID() string {
	return fmt.Sprintf("member_%d", time.Now().UnixNano())
}
