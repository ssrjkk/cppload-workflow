#pragma once

#include "cppload/net/http_client.hpp"
#include "cppload/metrics/collector.hpp"
#include <functional>
#include <string>
#include <vector>
#include <unordered_map>
#include <variant>
#include <chrono>

namespace cppload::scenario {

struct HttpStep {
    std::string method{"GET"};
    std::string path;
    std::string body;
    std::unordered_map<std::string, std::string> headers;
    std::string host;
    std::string port{"80"};
    std::vector<std::string> assertions;
    bool cache{false};
};

struct LoadProfile {
    struct Stage {
        std::string name;
        std::chrono::seconds duration{0};
        uint32_t target_rps{0};
    };
    
    std::vector<Stage> stages;
};

struct Scenario {
    std::string name;
    uint32_t weight{100};
    std::vector<HttpStep> steps;
};

struct SLAConfig {
    double max_error_rate{0.1};  // percent
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
    
    bool load_config();
    bool validate() const;
    
    const ScenarioConfig& config() const;
    
    using StepCallback = std::function<void(
        const HttpStep& step,
        const net::HttpResponse& response,
        metrics::MetricsCollector& metrics
    )>;
    
    void run(StepCallback callback = nullptr);
    void stop();

    void set_target_rps(uint32_t rps);
    uint32_t target_rps() const;

    bool check_sla(const metrics::MetricsCollector& metrics) const;
    
    std::string last_error() const;
    
private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace cppload::scenario
