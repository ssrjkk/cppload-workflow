// @author ssrjkk | volley
#pragma once

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace cppload::platform {

enum class RunStatus { pending, running, completed, failed };

enum class PolicySeverity { warning, error };

struct Project {
    std::string id;
    std::string name;
    std::string description;
    std::chrono::system_clock::time_point created_at;
};

struct Scenario {
    std::string id;
    std::string project_id;
    std::string name;
    std::string version;
    std::string yaml_content;
    std::chrono::system_clock::time_point created_at;
};

struct RunResult {
    int64_t total_requests = 0;
    int64_t successful_requests = 0;
    int64_t failed_requests = 0;
    double error_rate_pct = 0.0;
    double throughput_rps = 0.0;
    double p50_latency_ms = 0.0;
    double p95_latency_ms = 0.0;
    double p99_latency_ms = 0.0;
};

struct Run {
    std::string id;
    std::string project_id;
    std::string scenario_id;
    std::string environment;
    RunStatus status = RunStatus::pending;
    std::chrono::system_clock::time_point started_at;
    std::optional<std::chrono::system_clock::time_point> finished_at;
    std::optional<RunResult> result;
};

struct PolicyRule {
    std::string metric;
    std::string op;
    double threshold = 0.0;
};

struct Policy {
    std::string id;
    std::string project_id;
    std::string name;
    std::vector<PolicyRule> rules;
    std::chrono::system_clock::time_point created_at;
};

struct PolicyViolation {
    std::string rule_metric;
    std::string message;
    PolicySeverity severity = PolicySeverity::error;
};

struct PolicyEvaluation {
    std::string run_id;
    std::string policy_id;
    bool passed = false;
    std::vector<PolicyViolation> violations;
};

inline auto to_string(RunStatus s) -> std::string_view {
    switch (s) {
        case RunStatus::pending:   return "pending";
        case RunStatus::running:   return "running";
        case RunStatus::completed: return "completed";
        case RunStatus::failed:    return "failed";
    }
    return "unknown";
}

inline auto to_string(PolicySeverity s) -> std::string_view {
    switch (s) {
        case PolicySeverity::warning: return "warning";
        case PolicySeverity::error:   return "error";
    }
    return "unknown";
}

} // namespace cppload::platform
