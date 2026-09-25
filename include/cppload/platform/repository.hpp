// @author ssrjkk | volley
#pragma once

#include "cppload/platform/models.hpp"
#include "cppload/result.hpp"
#include <memory>
#include <string>
#include <vector>

namespace cppload::platform {

enum class PlatformError : std::uint8_t {
    not_found,
    already_exists,
    invalid_input,
    invalid_state
};

class Repository {
public:
    virtual ~Repository() = default;

    virtual auto create_project(std::string name, std::string description) -> Result<Project, PlatformError> = 0;
    [[nodiscard]] virtual auto get_project(const std::string& id) const -> Result<Project, PlatformError> = 0;
    [[nodiscard]] virtual auto list_projects() const -> std::vector<Project> = 0;
    virtual auto delete_project(const std::string& id) -> Result<bool, PlatformError> = 0;

    virtual auto create_scenario(std::string project_id, std::string name,
                                 std::string version, std::string yaml_content) -> Result<Scenario, PlatformError> = 0;
    [[nodiscard]] virtual auto get_scenario(const std::string& id) const -> Result<Scenario, PlatformError> = 0;
    [[nodiscard]] virtual auto list_scenarios(const std::string& project_id) const -> std::vector<Scenario> = 0;

    virtual auto create_run(std::string project_id, std::string scenario_id, std::string environment) -> Result<Run, PlatformError> = 0;
    [[nodiscard]] virtual auto get_run(const std::string& id) const -> Result<Run, PlatformError> = 0;
    [[nodiscard]] virtual auto list_runs(const std::string& project_id) const -> std::vector<Run> = 0;
    virtual auto finish_run(const std::string& id, RunStatus status, RunResult result) -> Result<bool, PlatformError> = 0;

    virtual auto create_policy(std::string project_id, std::string name, std::vector<PolicyRule> rules) -> Result<Policy, PlatformError> = 0;
    [[nodiscard]] virtual auto get_policy(const std::string& id) const -> Result<Policy, PlatformError> = 0;
    [[nodiscard]] virtual auto list_policies(const std::string& project_id) const -> std::vector<Policy> = 0;
};

auto make_in_memory_repository() -> std::shared_ptr<Repository>;

} // namespace cppload::platform
