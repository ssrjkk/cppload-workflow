#include "cppload/scenario/engine.hpp"
#include "cppload/core/token_bucket.hpp"
#include "cppload/core/url_parse.hpp"
#include "cppload/net/protocol_factory.hpp"
#include <boost/asio/io_context.hpp>
#include <atomic>
#include <mutex>
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
        std::lock_guard<std::mutex> lock(ioc_mutex_);
        if (active_ioc_) active_ioc_->stop();
    }

    void set_target_rps(uint32_t rps) {
        target_rps_.store(rps > 0 ? rps : 100, std::memory_order_relaxed);
        bucket_.set_rate(static_cast<double>(target_rps_.load(std::memory_order_relaxed)));
    }

    uint32_t target_rps() const { return target_rps_.load(std::memory_order_relaxed); }

    bool load_config();

    bool validate() const {
        {
            std::lock_guard<std::mutex> lock(last_error_mtx_);
            if (config_.test_id.empty()) {
                last_error_ = "test_id is required";
                return false;
            }
            if (config_.target.base_url.empty()) {
                last_error_ = "target.base_url is required";
                return false;
            }
        }
        return true;
    }

    const ScenarioConfig& config() const { return config_; }

    void run(StepCallback callback) {
        stopped_ = false;
        auto ioc = std::make_shared<boost::asio::io_context>();
        {
            std::lock_guard<std::mutex> lock(ioc_mutex_);
            active_ioc_ = ioc.get();
        }

        // Take a local copy of config for thread safety
        auto cfg = config_;

        std::string proto_name = cfg.target.protocol;
        if (proto_name.empty()) proto_name = "http1.1";

        // Configure TLS
        cppload::security::TlsConfig tls_cfg;
        tls_cfg.verify_peer = cfg.target.tls.verify;
        net::ProtocolFactory::set_tls_config(tls_cfg);

        uint32_t concurrency = 10;
        if (!cfg.load_profile.stages.empty()) {
            concurrency = cfg.load_profile.stages[0].concurrent_users;
        }

        metrics::MetricsCollector metrics;
        std::vector<std::thread> workers;

        for (uint32_t w = 0; w < concurrency; ++w) {
            workers.emplace_back([this, ioc, &metrics, &callback, proto_name, cfg]() {
                try {
                    auto client = net::ProtocolFactory::create(proto_name, *ioc);
                    if (!client) {
                        {
                            std::lock_guard<std::mutex> lock(last_error_mtx_);
                            last_error_ = "unsupported protocol: " + proto_name;
                        }
                        return;
                    }

                    for (const auto& scenario : cfg.scenarios) {
                        if (stopped_) break;
                        for (const auto& step : scenario.steps) {
                            if (stopped_) break;

                            if (!bucket_.try_consume_for(std::chrono::milliseconds(10))) {
                                continue;
                            }

                            net::Request req;
                            req.method = step.method;
                            req.path = step.path;
                            req.body = step.body;
                            req.headers = step.headers;
                            parse_url(cfg.target.base_url, req.host, req.port,
                                      req.use_tls, step.use_tls);

                            auto capture_req = std::make_shared<net::Request>(std::move(req));
                            auto done = std::make_shared<std::atomic<bool>>(false);
                            client->async_request(*capture_req,
                                [capture_req, &metrics, &callback, done, step](std::error_code ec, net::Response resp) mutable {
                                    metrics.record_request(resp.status_code, resp.latency,
                                                           capture_req->body.size(), resp.body.size());
                                    if (callback) {
                                        callback(step, resp, metrics);
                                    }
                                    done->store(true, std::memory_order_release);
                                });

                            while (!done->load(std::memory_order_acquire) && !stopped_) {
                                ioc->run_one();
                            }
                        }
                    }
                } catch (const std::exception& e) {
                    std::lock_guard<std::mutex> lock(last_error_mtx_);
                    last_error_ = "worker thread error: " + std::string(e.what());
                }
            });
        }

        for (auto& t : workers) {
            if (t.joinable()) t.join();
        }

        {
            std::lock_guard<std::mutex> lock(ioc_mutex_);
            active_ioc_ = nullptr;
        }
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

    std::string last_error() const {
        std::lock_guard<std::mutex> lock(last_error_mtx_);
        return last_error_;
    }

private:
    void parse_url(const std::string& url, std::string& host,
                   uint16_t& port, bool& use_tls, bool step_tls) {
        auto parts = core::parse_url(url);
        host = std::move(parts.host);
        use_tls = parts.tls || step_tls;

        if (!parts.port.empty()) {
            try {
                auto p = std::stoul(parts.port);
                if (p == 0 || p > 65535) {
                    std::lock_guard<std::mutex> lock(last_error_mtx_);
                    last_error_ = "port out of range: " + parts.port;
                    port = use_tls ? 443 : 80;
                } else {
                    port = static_cast<uint16_t>(p);
                }
            } catch (const std::exception&) {
                std::lock_guard<std::mutex> lock(last_error_mtx_);
                last_error_ = "invalid port: " + parts.port;
                port = use_tls ? 443 : 80;
            }
        } else {
            port = use_tls ? 443 : 80;
        }
    }

    mutable std::mutex ioc_mutex_;
    boost::asio::io_context* active_ioc_{nullptr};
    std::atomic<bool> stopped_{false};
    std::string config_path_;
    ScenarioConfig config_;
    mutable std::mutex last_error_mtx_;
    mutable std::string last_error_;
    TokenBucket bucket_;
    std::atomic<uint32_t> target_rps_{100};
};

// Defined in yaml_parser.cpp
bool parse_config_file(const std::string& path, ScenarioConfig& config, std::string& error);

bool ScenarioEngine::Impl::load_config() {
    std::string error;
    if (!parse_config_file(config_path_, config_, error)) {
        std::lock_guard<std::mutex> lock(last_error_mtx_);
        last_error_ = error;
        return false;
    }
    return true;
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
