#include "cppload/scenario/engine.hpp"
#include "cppload/core/token_bucket.hpp"
#include "cppload/net/protocol_factory.hpp"
#include <boost/asio/io_context.hpp>
#include <atomic>
#include <thread>
#include <memory>

namespace cppload::scenario {

class ScenarioEngine::Impl {
public:
    explicit Impl(const std::string& config_path)
        : config_path_(config_path)
        , bucket_(100.0) {}

    void stop() {
        stopped_ = true;
        if (active_ioc_) active_ioc_->stop();
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
        stopped_ = false;
        boost::asio::io_context ioc;
        active_ioc_ = &ioc;

        std::string proto_name = config_.target.protocol;
        if (proto_name.empty()) proto_name = "http1.1";

        // Configure TLS
        cppload::security::TlsConfig tls_cfg;
        tls_cfg.verify_peer = config_.target.tls.verify;
        net::ProtocolFactory::set_tls_config(tls_cfg);

        uint32_t concurrency = 10;
        if (!config_.load_profile.stages.empty()) {
            concurrency = config_.load_profile.stages[0].concurrent_users;
        }

        metrics::MetricsCollector metrics;
        std::vector<std::thread> workers;

        for (uint32_t w = 0; w < concurrency; ++w) {
            workers.emplace_back([this, &ioc, &metrics, &callback, &proto_name]() {
                auto client = net::ProtocolFactory::create(proto_name, ioc);
                if (!client) {
                    last_error_ = "unsupported protocol: " + proto_name;
                    return;
                }

                for (const auto& scenario : config_.scenarios) {
                    if (stopped_) break;
                    for (const auto& step : scenario.steps) {
                        if (stopped_) break;

                        bucket_.consume();

                        net::Request req;
                        req.method = step.method;
                        req.path = step.path;
                        req.body = step.body;
                        req.headers = step.headers;
                        parse_url(config_.target.base_url, req.host, req.port,
                                  req.use_tls, step.use_tls);

                        std::atomic<bool> done{false};
                        client->async_request(req,
                            [&, step](std::error_code ec, net::Response resp) mutable {
                                metrics.record_request(static_cast<uint16_t>(resp.status_code), resp.latency,
                                                       req.body.size(), resp.body.size());
                                if (callback) {
                                    callback(step, resp, metrics);
                                }
                                done = true;
                            });

                        while (!done && !stopped_) {
                            ioc.run_one();
                        }
                    }
                }
            });
        }

        for (auto& t : workers) {
            if (t.joinable()) t.join();
        }

        active_ioc_ = nullptr;
    }

    bool check_sla(const metrics::MetricsCollector& m) const {
        if (m.error_rate() > config_.sla.max_error_rate) {
            return false;
        }
        auto snap = m.snapshot();
        if (snap.p99_latency_us > config_.sla.max_p99_latency.count() * 1000) {
            return false;
        }
        return true;
    }

    std::string last_error() const { return last_error_; }

private:
    void parse_url(const std::string& url, std::string& host,
                   uint16_t& port, bool& use_tls, bool step_tls) {
        bool is_https = false;
        auto proto_end = url.find("://");
        if (proto_end != std::string::npos) {
            auto scheme = url.substr(0, proto_end);
            is_https = (scheme == "https");
        }
        auto start = (proto_end != std::string::npos) ? proto_end + 3 : 0;
        auto path_start = url.find("/", start);
        auto host_port_str = (path_start != std::string::npos)
            ? url.substr(start, path_start - start)
            : url.substr(start);

        use_tls = is_https || step_tls;

        auto colon = host_port_str.find(":");
        if (colon != std::string::npos) {
            host = host_port_str.substr(0, colon);
            std::string port_str = host_port_str.substr(colon + 1);
            try {
                auto p = std::stoul(port_str);
                if (p == 0 || p > 65535) {
                    last_error_ = "port out of range: " + port_str;
                    port = use_tls ? 443 : 80;
                } else {
                    port = static_cast<uint16_t>(p);
                }
            } catch (const std::exception&) {
                last_error_ = "invalid port: " + port_str;
                port = use_tls ? 443 : 80;
            }
        } else {
            host = host_port_str;
            port = use_tls ? 443 : 80;
        }
    }

    boost::asio::io_context* active_ioc_{nullptr};
    std::atomic<bool> stopped_{false};
    std::string config_path_;
    ScenarioConfig config_;
    mutable std::string last_error_;
    TokenBucket bucket_;
    uint32_t target_rps_{100};
};

// Defined in yaml_parser.cpp
bool parse_config_file(const std::string& path, ScenarioConfig& config, std::string& error);

bool ScenarioEngine::Impl::load_config() {
    return parse_config_file(config_path_, config_, last_error_);
}

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
