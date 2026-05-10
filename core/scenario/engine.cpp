#include "cppload/scenario/engine.hpp"
#include "cppload/core/token_bucket.hpp"
#include <boost/asio/io_context.hpp>

namespace cppload::scenario {

class ScenarioEngine::Impl {
public:
    explicit Impl(const std::string& config_path)
        : config_path_(config_path)
        , bucket_(100.0) {}
    
    void stop() {
        ioc_.stop();
    }
    
    void set_target_rps(uint32_t rps) {
        target_rps_ = rps > 0 ? rps : 100;
        bucket_.set_rate(static_cast<double>(target_rps_));
    }
    
    uint32_t target_rps() const { return target_rps_; }
    
    bool load_config();
    
    bool validate() const {
        if (config_.test_id.empty()) {
            last_error_ = "test_id is required";
            return false;
        }
        if (config_.target.base_url.empty()) {
            last_error_ = "target.base_url is required";
            return false;
        }
        return true;
    }
    
    const ScenarioConfig& config() const { return config_; }
    
    void run(StepCallback callback) {
        // MVP: execute scenarios sequentially
        net::HttpClient client(ioc_);
        metrics::MetricsCollector metrics;
        
        for (const auto& scenario : config_.scenarios) {
            if (ioc_.stopped()) break;
            for (const auto& step : scenario.steps) {
                if (ioc_.stopped()) break;
                net::HttpRequest req;
                req.method = step.method;
                req.target = step.path;
                req.body = step.body;
                req.headers = step.headers;
                
                // Extract host and port from base_url
                parse_url(config_.target.base_url, req.host, req.port);
                
                client.async_request(req, [&](const auto& resp) {
                    metrics.record_request(resp.status_code, resp.latency,
                                         req.body.size(), resp.body.size());
                    if (callback) {
                        callback(step, resp, metrics);
                    }
                });
                
                bucket_.consume();
            }
        }
        
        // Blocks until all async operations complete (or stop() is called)
        if (!ioc_.stopped()) ioc_.run();
        ioc_.restart();
    }
    
    bool check_sla(const metrics::MetricsCollector& m) const {
        if (m.error_rate() > config_.sla.max_error_rate) {
            return false;
        }
        if (m.p99_latency_us() > config_.sla.max_p99_latency.count() * 1000) {
            return false;
        }
        return true;
    }
    
    std::string last_error() const { return last_error_; }
    
private:
    void parse_url(const std::string& url, std::string& host, std::string& port) {
        // Simple URL parser
        auto proto_end = url.find("://");
        auto start = (proto_end != std::string::npos) ? proto_end + 3 : 0;
        auto path_start = url.find("/", start);
        auto host_port = (path_start != std::string::npos) 
            ? url.substr(start, path_start - start) 
            : url.substr(start);
        
        auto colon = host_port.find(":");
        if (colon != std::string::npos) {
            host = host_port.substr(0, colon);
            port = host_port.substr(colon + 1);
        } else {
            host = host_port;
            port = "80";
        }
    }
    
    boost::asio::io_context ioc_;
    std::string config_path_;
    ScenarioConfig config_;
    std::string last_error_;
    TokenBucket bucket_;
    uint32_t target_rps_{100};
};

ScenarioEngine::ScenarioEngine(const std::string& config_path)
    : impl_(std::make_unique<Impl>(config_path)) {}

ScenarioEngine::~ScenarioEngine() noexcept = default;

bool ScenarioEngine::load_config() { return impl_->load_config(); }
bool ScenarioEngine::validate() const { return impl_->validate(); }
const ScenarioConfig& ScenarioEngine::config() const { return impl_->config(); }
void ScenarioEngine::run(StepCallback callback) { impl_->run(callback); }
void ScenarioEngine::stop() { impl_->stop(); }
void ScenarioEngine::set_target_rps(uint32_t rps) { impl_->set_target_rps(rps); }
uint32_t ScenarioEngine::target_rps() const { return impl_->target_rps(); }
bool ScenarioEngine::check_sla(const metrics::MetricsCollector& m) const { return impl_->check_sla(m); }
std::string ScenarioEngine::last_error() const { return impl_->last_error(); }

} // namespace cppload::scenario
