// @author ssrjkk | volley
#pragma once

#include "cppload/platform/models.hpp"
#include "cppload/platform/repository.hpp"
#include <memory>
#include <optional>
#include <string>

namespace cppload::platform {

class PolicyEvaluator final {
public:
    explicit PolicyEvaluator(std::shared_ptr<Repository> repo);

    auto evaluate(const std::string& run_id, const std::string& policy_id) -> PolicyEvaluation;

private:
    auto get_metric_value(const RunResult& r, const std::string& metric) const -> std::optional<double>;
    auto check_threshold(double actual, const std::string& op, double threshold) const -> bool;
    auto format_violation(const std::string& metric, const std::string& op,
                          double threshold, double actual) const -> std::string;

    std::shared_ptr<Repository> repo_;
};

} // namespace cppload::platform
