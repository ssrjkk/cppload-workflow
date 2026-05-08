#include "cppload/scenario/engine.hpp"
#include <fstream>
#include <sstream>
#include <algorithm>
#include <numeric>

namespace cppload::scenario {

class ScenarioEngine::Impl {
public:
    explicit Impl(const std::string& config_path)
        : config_path_(config_path) {}
    
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
        boost::asio::io_context ioc;
        net::HttpClient client(ioc);
        metrics::MetricsCollector metrics;
        
        auto worker = std::thread([&ioc]() { ioc.run(); });
        
        for (const auto& scenario : config_.scenarios) {
            for (const auto& step : scenario.steps) {
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
                
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
        }
        
        ioc.stop();
        worker.join();
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
    std::string extract_value(const std::string& content, const std::string& key) {
        auto pos = content.find(key + ":");
        if (pos == std::string::npos) return "";
        
        auto line_start = content.find(":", pos) + 1;
        auto line_end = content.find("\n", line_start);
        if (line_end == std::string::npos) line_end = content.length();
        
        std::string value = content.substr(line_start, line_end - line_start);
        
        // Remove quotes and whitespace
        value.erase(0, value.find_first_not_of(" \t\""));
        value.erase(value.find_last_not_of(" \t\"\n") + 1);
        
        // Handle environment variables ${VAR:-default}
        if (value.find("${") == 0) {
            auto end = value.find("}");
            if (end != std::string::npos) {
                std::string var_expr = value.substr(2, end - 2);
                auto colon = var_expr.find(":-");
                if (colon != std::string::npos) {
                    std::string var_name = var_expr.substr(0, colon);
                    std::string default_val = var_expr.substr(colon + 2);
                    
                    const char* env_val = getenv(var_name.c_str());
                    return env_val ? env_val : default_val;
                }
            }
        }
        
        return value;
    }
    
    std::string extract_section(const std::string& content, const std::string& section) {
        auto pos = content.find(section + ":");
        if (pos == std::string::npos) return "";
        
        auto section_start = pos + section.length() + 1;
        auto next_section = content.find("\n", section_start);
        
        // Find next top-level key
        for (size_t i = section_start; i < content.length(); ) {
            auto newline = content.find("\n", i);
            if (newline == std::string::npos) break;
            
            auto line = content.substr(newline + 1);
            if (line.length() > 0 && (line[0] != ' ' && line[0] != '\t')) {
                next_section = newline;
                break;
            }
            i = newline + 1;
        }
        
        if (next_section == std::string::npos) {
            return content.substr(section_start);
        }
        
        return content.substr(section_start, next_section - section_start);
    }
    
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
    
    std::string config_path_;
    ScenarioConfig config_;
    std::string last_error_;
};

ScenarioEngine::ScenarioEngine(const std::string& config_path)
    : impl_(std::make_unique<Impl>(config_path)) {}

ScenarioEngine::~ScenarioEngine() = default;

bool ScenarioEngine::load_config() { return impl_->load_config(); }
bool ScenarioEngine::validate() const { return impl_->validate(); }
const ScenarioConfig& ScenarioEngine::config() const { return impl_->config(); }
void ScenarioEngine::run(StepCallback callback) { impl_->run(callback); }
bool ScenarioEngine::check_sla(const metrics::MetricsCollector& m) const { return impl_->check_sla(m); }
std::string ScenarioEngine::last_error() const { return impl_->last_error(); }

} // namespace cppload::scenario
