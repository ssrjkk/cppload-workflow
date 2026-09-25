// @author ssrjkk | volley
#include "cppload/platform/repository.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <iomanip>
#include <mutex>
#include <random>
#include <sstream>
#include <unordered_map>

namespace cppload::platform {
namespace {

class InMemoryRepository final : public Repository {
public:
    InMemoryRepository() = default;

    auto create_project(std::string name, std::string description) -> Result<Project, PlatformError> override {
        if (name.empty()) {
            return Result<Project, PlatformError>::err(PlatformError::invalid_input);
        }
        auto proj = Project{
            generate_id(),
            std::move(name),
            std::move(description),
            std::chrono::system_clock::now()
        };
        std::lock_guard lock(mtx_);
        projects_[proj.id] = proj;
        return Result<Project, PlatformError>::ok(std::move(proj));
    }

    auto get_project(const std::string& id) const -> Result<Project, PlatformError> override {
        std::lock_guard lock(mtx_);
        auto it = projects_.find(id);
        if (it == projects_.end()) {
            return Result<Project, PlatformError>::err(PlatformError::not_found);
        }
        return Result<Project, PlatformError>::ok(it->second);
    }

    auto list_projects() const -> std::vector<Project> override {
        std::lock_guard lock(mtx_);
        auto result = std::vector<Project>{};
        result.reserve(projects_.size());
        for (const auto& [_, proj] : projects_) {
            result.push_back(proj);
        }
        std::sort(result.begin(), result.end(),
                  [](const Project& a, const Project& b) { return a.created_at > b.created_at; });
        return result;
    }

    auto delete_project(const std::string& id) -> Result<bool, PlatformError> override {
        std::lock_guard lock(mtx_);
        auto it = projects_.find(id);
        if (it == projects_.end()) {
            return Result<bool, PlatformError>::err(PlatformError::not_found);
        }
        projects_.erase(it);
        std::erase_if(scenarios_, [&](const auto& p) { return p.second.project_id == id; });
        std::erase_if(runs_, [&](const auto& p) { return p.second.project_id == id; });
        std::erase_if(policies_, [&](const auto& p) { return p.second.project_id == id; });
        return Result<bool, PlatformError>::ok(true);
    }

    auto create_scenario(std::string project_id, std::string name,
                         std::string version, std::string yaml_content) -> Result<Scenario, PlatformError> override {
        if (project_id.empty() || name.empty()) {
            return Result<Scenario, PlatformError>::err(PlatformError::invalid_input);
        }
        {
            std::lock_guard lock(mtx_);
            if (!projects_.contains(project_id)) {
                return Result<Scenario, PlatformError>::err(PlatformError::not_found);
            }
        }
        auto sc = Scenario{
            generate_id(),
            std::move(project_id),
            std::move(name),
            std::move(version),
            std::move(yaml_content),
            std::chrono::system_clock::now()
        };
        std::lock_guard lock(mtx_);
        scenarios_[sc.id] = sc;
        return Result<Scenario, PlatformError>::ok(std::move(sc));
    }

    auto get_scenario(const std::string& id) const -> Result<Scenario, PlatformError> override {
        std::lock_guard lock(mtx_);
        auto it = scenarios_.find(id);
        if (it == scenarios_.end()) {
            return Result<Scenario, PlatformError>::err(PlatformError::not_found);
        }
        return Result<Scenario, PlatformError>::ok(it->second);
    }

    auto list_scenarios(const std::string& project_id) const -> std::vector<Scenario> override {
        std::lock_guard lock(mtx_);
        auto result = std::vector<Scenario>{};
        for (const auto& [_, sc] : scenarios_) {
            if (sc.project_id == project_id) {
                result.push_back(sc);
            }
        }
        std::sort(result.begin(), result.end(),
                  [](const Scenario& a, const Scenario& b) { return a.created_at > b.created_at; });
        return result;
    }

    auto create_run(std::string project_id, std::string scenario_id,
                    std::string environment) -> Result<Run, PlatformError> override {
        if (project_id.empty()) {
            return Result<Run, PlatformError>::err(PlatformError::invalid_input);
        }
        {
            std::lock_guard lock(mtx_);
            if (!projects_.contains(project_id)) {
                return Result<Run, PlatformError>::err(PlatformError::not_found);
            }
        }
        auto run = Run{
            generate_id(),
            std::move(project_id),
            std::move(scenario_id),
            std::move(environment),
            RunStatus::pending,
            std::chrono::system_clock::now(),
            std::nullopt,
            std::nullopt
        };
        std::lock_guard lock(mtx_);
        runs_[run.id] = run;
        return Result<Run, PlatformError>::ok(std::move(run));
    }

    auto get_run(const std::string& id) const -> Result<Run, PlatformError> override {
        std::lock_guard lock(mtx_);
        auto it = runs_.find(id);
        if (it == runs_.end()) {
            return Result<Run, PlatformError>::err(PlatformError::not_found);
        }
        return Result<Run, PlatformError>::ok(it->second);
    }

    auto list_runs(const std::string& project_id) const -> std::vector<Run> override {
        std::lock_guard lock(mtx_);
        auto result = std::vector<Run>{};
        for (const auto& [_, run] : runs_) {
            if (run.project_id == project_id) {
                result.push_back(run);
            }
        }
        std::sort(result.begin(), result.end(),
                  [](const Run& a, const Run& b) { return a.started_at > b.started_at; });
        return result;
    }

    auto finish_run(const std::string& id, RunStatus status,
                    RunResult result) -> Result<bool, PlatformError> override {
        std::lock_guard lock(mtx_);
        auto it = runs_.find(id);
        if (it == runs_.end()) {
            return Result<bool, PlatformError>::err(PlatformError::not_found);
        }
        if (it->second.status == RunStatus::completed || it->second.status == RunStatus::failed) {
            return Result<bool, PlatformError>::err(PlatformError::invalid_state);
        }
        it->second.status = status;
        it->second.finished_at = std::chrono::system_clock::now();
        it->second.result = std::move(result);
        return Result<bool, PlatformError>::ok(true);
    }

    auto create_policy(std::string project_id, std::string name,
                       std::vector<PolicyRule> rules) -> Result<Policy, PlatformError> override {
        if (project_id.empty() || name.empty() || rules.empty()) {
            return Result<Policy, PlatformError>::err(PlatformError::invalid_input);
        }
        {
            std::lock_guard lock(mtx_);
            if (!projects_.contains(project_id)) {
                return Result<Policy, PlatformError>::err(PlatformError::not_found);
            }
        }
        auto pol = Policy{
            generate_id(),
            std::move(project_id),
            std::move(name),
            std::move(rules),
            std::chrono::system_clock::now()
        };
        std::lock_guard lock(mtx_);
        policies_[pol.id] = pol;
        return Result<Policy, PlatformError>::ok(std::move(pol));
    }

    auto get_policy(const std::string& id) const -> Result<Policy, PlatformError> override {
        std::lock_guard lock(mtx_);
        auto it = policies_.find(id);
        if (it == policies_.end()) {
            return Result<Policy, PlatformError>::err(PlatformError::not_found);
        }
        return Result<Policy, PlatformError>::ok(it->second);
    }

    auto list_policies(const std::string& project_id) const -> std::vector<Policy> override {
        std::lock_guard lock(mtx_);
        auto result = std::vector<Policy>{};
        for (const auto& [_, pol] : policies_) {
            if (pol.project_id == project_id) {
                result.push_back(pol);
            }
        }
        return result;
    }

private:
    static auto generate_id() -> std::string {
        static std::atomic<uint64_t> counter{0};
        auto now = std::chrono::steady_clock::now().time_since_epoch();
        auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(now).count();
        auto seq = counter.fetch_add(1, std::memory_order_relaxed);

        thread_local std::mt19937_64 rng{std::random_device{}()};
        auto rand_part = rng() & 0xFFFF;

        auto oss = std::ostringstream{};
        oss << std::hex << std::setfill('0')
            << std::setw(8) << static_cast<uint32_t>(ns & 0xFFFFFFFF)
            << std::setw(4) << static_cast<uint16_t>(seq & 0xFFFF)
            << std::setw(4) << static_cast<uint16_t>(rand_part);
        return oss.str();
    }

    mutable std::mutex mtx_;
    std::unordered_map<std::string, Project> projects_;
    std::unordered_map<std::string, Scenario> scenarios_;
    std::unordered_map<std::string, Run> runs_;
    std::unordered_map<std::string, Policy> policies_;
};

} // namespace

auto make_in_memory_repository() -> std::shared_ptr<Repository> {
    return std::make_shared<InMemoryRepository>();
}

} // namespace cppload::platform
