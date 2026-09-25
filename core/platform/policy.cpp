// @author ssrjkk | volley
#include "cppload/platform/policy.hpp"

#include <cmath>

namespace cppload::platform {

PolicyEvaluator::PolicyEvaluator(std::shared_ptr<Repository> repo)
    : repo_(std::move(repo)) {}

auto PolicyEvaluator::evaluate(const std::string& run_id,
                               const std::string& policy_id) -> PolicyEvaluation {
    auto evaluation = PolicyEvaluation{run_id, policy_id, true, {}};

    auto run_res = repo_->get_run(run_id);
    if (!run_res.has_value()) {
        evaluation.passed = false;
        evaluation.violations.push_back(
            {"run", "run not found: " + run_id, PolicySeverity::error});
        return evaluation;
    }

    auto policy_res = repo_->get_policy(policy_id);
    if (!policy_res.has_value()) {
        evaluation.passed = false;
        evaluation.violations.push_back(
            {"policy", "policy not found: " + policy_id, PolicySeverity::error});
        return evaluation;
    }

    const auto& run = run_res.value();
    const auto& policy = policy_res.value();

    if (!run.result.has_value()) {
        evaluation.passed = false;
        evaluation.violations.push_back(
            {"result", "run has no result yet", PolicySeverity::error});
        return evaluation;
    }

    const auto& result = run.result.value();

    for (const auto& rule : policy.rules) {
        auto actual = get_metric_value(result, rule.metric);
        if (!actual.has_value()) {
            evaluation.violations.push_back(
                {rule.metric, "unknown metric: " + rule.metric, PolicySeverity::warning});
            continue;
        }

        auto violated = check_threshold(actual.value(), rule.op, rule.threshold);
        if (violated) {
            evaluation.passed = false;
            auto msg = format_violation(rule.metric, rule.op, rule.threshold, actual.value());
            evaluation.violations.push_back({rule.metric, std::move(msg), PolicySeverity::error});
        }
    }

    return evaluation;
}

auto PolicyEvaluator::get_metric_value(const RunResult& r,
                                       const std::string& metric) const -> std::optional<double> {
    if (metric == "error_rate_pct") return r.error_rate_pct;
    if (metric == "throughput_rps") return r.throughput_rps;
    if (metric == "p50_latency_ms") return r.p50_latency_ms;
    if (metric == "p95_latency_ms") return r.p95_latency_ms;
    if (metric == "p99_latency_ms") return r.p99_latency_ms;
    if (metric == "total_requests") return static_cast<double>(r.total_requests);
    if (metric == "failed_requests") return static_cast<double>(r.failed_requests);
    return std::nullopt;
}

auto PolicyEvaluator::check_threshold(double actual, const std::string& op,
                                      double threshold) const -> bool {
    if (op == "<")  return !(actual < threshold);
    if (op == "<=") return !(actual <= threshold);
    if (op == ">")  return !(actual > threshold);
    if (op == ">=") return !(actual >= threshold);
    if (op == "==") return std::fabs(actual - threshold) > 1e-9;
    if (op == "!=") return std::fabs(actual - threshold) <= 1e-9;
    return true;
}

auto PolicyEvaluator::format_violation(const std::string& metric, const std::string& op,
                                       double threshold, double actual) const -> std::string {
    return metric + " expected " + op + " " + std::to_string(threshold)
           + " but got " + std::to_string(actual);
}

} // namespace cppload::platform
