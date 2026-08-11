// @author ssrjkk | cppload
#pragma once

#include "cppload/net/protocol.hpp"
#include "cppload/metrics/collector.hpp"
#include "cppload/security/auth_provider.hpp"
#include <functional>
#include <string>
#include <vector>
#include <unordered_map>
#include <variant>
#include <chrono>
#include <cstdint>
#include <memory>

namespace cppload::scenario {

struct HttpStep {
    std::string method{"GET"};
    std::string path;
    std::string body;
    std::unordered_map<std::string, std::string> headers;
    std::string host;
    std::string port{"80"};
    bool use_tls{false};
    // Assertions like "status_code == 201". A failed assertion marks the
    // request as failed for SLA/metrics purposes.
    std::vector<std::string> assertions;
};

// Evaluates all assertions of a step against the response.
// Returns true when the step has no assertions or all of them pass.
bool evaluate_assertions(const HttpStep& step, const net::Response& response);

struct LoadProfile {
    struct Stage {
        std::string name;
        std::chrono::milliseconds duration{0};
        uint32_t target_rps{0};
        uint32_t concurrent_users{10};
    };

    std::vector<Stage> stages;
};

struct Scenario {
    std::string name;
    uint32_t weight{100};
    std::vector<HttpStep> steps;
};

struct ScenarioAuthConfig {
    std::string type;
    std::string token_endpoint;
    struct {
        std::string client_id;
        std::string client_secret;
    } client_credentials;
};

struct ObservabilityConfig {
    struct {
        struct {
            bool enabled{false};
            uint16_t port{9090};
        } prometheus;
    } metrics;
    struct {
        std::string otlp_endpoint;
        double sample_rate{0.1};
    } tracing;
    struct {
        std::string level{"info"};
        std::string format{"json"};
    } logging;
};

struct SLAConfig {
    double max_error_rate{0.1};
    std::chrono::milliseconds max_p99_latency{500};
};

struct ScenarioConfig {
    std::string version;
    std::string test_id;

    struct {
        std::string base_url;
        std::string protocol{"http1.1"};
        struct {
            bool verify{true};
        } tls;
    } target;

    ScenarioAuthConfig authentication;
    ObservabilityConfig observability;
    LoadProfile load_profile;
    std::vector<Scenario> scenarios;
    SLAConfig sla;
};

class ScenarioEngine {
public:
    explicit ScenarioEngine(const std::string& config_path);
    ~ScenarioEngine() noexcept;

    ScenarioEngine(const ScenarioEngine&) = delete;
    ScenarioEngine& operator=(const ScenarioEngine&) = delete;
    ScenarioEngine(ScenarioEngine&&) = delete;
    ScenarioEngine& operator=(ScenarioEngine&&) = delete;

    [[nodiscard]] bool load_config();
    [[nodiscard]] bool validate() const;

    [[nodiscard]] const ScenarioConfig& config() const;

    using StepCallback = std::function<void(
        const HttpStep& step,
        const net::Response& response,
        metrics::MetricsCollector& metrics
    )>;

    void run(StepCallback callback = nullptr);
    void stop();

    void set_target_rps(uint32_t rps);
    [[nodiscard]] uint32_t target_rps() const;

    // Optional global cap on the whole test run. Stages still run in order,
    // but the run ends once the cap is reached. 0 disables the cap.
    void set_max_duration(std::chrono::milliseconds duration);
    [[nodiscard]] std::chrono::milliseconds max_duration() const;

    // Optional auth provider; its header (Bearer/API key) is applied to every request.
    void set_auth_provider(std::shared_ptr<security::AuthProvider> auth);

    [[nodiscard]] bool check_sla(const metrics::MetricsCollector& metrics) const;

    [[nodiscard]] std::string last_error() const;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace cppload::scenario