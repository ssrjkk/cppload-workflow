// @author ssrjkk | cppload
#include "cppload/scenario/engine.hpp"
#include "cppload/core/constants.hpp"
#include "cppload/core/token_bucket.hpp"
#include "cppload/core/url_parse.hpp"
#include "cppload/net/protocol_factory.hpp"
#include "cppload/security/auth_provider.hpp"
#include <boost/asio/io_context.hpp>
#include <boost/asio/executor_work_guard.hpp>
#include <atomic>
#include <cctype>
#include <mutex>
#include <random>
#include <thread>
#include <memory>
#include <sstream>

namespace cppload::scenario {

namespace {

// Parses "<number><unit>" where unit is us|ms|s|m|h. Returns microseconds or -1.
long long parse_duration_us(const std::string& str) {
    if (str.empty()) return -1;
    size_t pos = 0;
    while (pos < str.length() && std::isdigit(static_cast<unsigned char>(str[pos]))) ++pos;
    if (pos == 0) return -1;
    long long value = 0;
    try {
        value = std::stoll(str.substr(0, pos));
    } catch (const std::exception&) {
        return -1;
    }
    std::string unit = str.substr(pos);
    if (unit == "us") return value;
    if (unit == "ms") return value * 1000;
    if (unit == "s") return value * 1000 * 1000;
    if (unit == "m") return value * 60 * 1000 * 1000;
    if (unit == "h") return value * 3600 * 1000 * 1000;
    return -1;
}

template <typename T>
bool compare_op(T actual, const std::string& op, T expected) {
    if (op == "==") return actual == expected;
    if (op == "!=") return actual != expected;
    if (op == ">") return actual > expected;
    if (op == ">=") return actual >= expected;
    if (op == "<") return actual < expected;
    if (op == "<=") return actual <= expected;
    return false;
}

bool evaluate_assertion(const std::string& expr, const net::Response& response) {
    std::string lhs, op, rhs;
    std::istringstream ss(expr);
    ss >> lhs >> op >> rhs;
    if (op.empty() || rhs.empty()) return false;

    if (lhs == "status_code") {
        long expected = 0;
        try { expected = std::stol(rhs); }
        catch (const std::exception&) { return false; }
        return compare_op(static_cast<long>(response.status_code), op, expected);
    }
    if (lhs == "latency") {
        long long expected_us = parse_duration_us(rhs);
        if (expected_us < 0) return false;
        return compare_op(static_cast<long long>(response.latency.count()), op, expected_us);
    }
    return false;
}

// Weighted random scenario selection. A scenario with weight 0 (or a missing
// weight) behaves as weight 1 so it never starves. Returns nullptr when the
// scenario list is empty.
const Scenario* pick_weighted_scenario(const std::vector<Scenario>& scenarios,
                                       std::mt19937& rng) {
    if (scenarios.empty()) return nullptr;
    uint64_t total = 0;
    for (const auto& s : scenarios) total += (s.weight > 0 ? s.weight : 1);
    uint64_t pick = std::uniform_int_distribution<uint64_t>(0, total - 1)(rng);
    for (const auto& s : scenarios) {
        uint64_t w = (s.weight > 0 ? s.weight : 1);
        if (pick < w) return &s;
        pick -= w;
    }
    return &scenarios.back();
}

} // anonymous namespace

bool evaluate_assertions(const HttpStep& step, const net::Response& response) {
    for (const auto& expr : step.assertions) {
        if (!evaluate_assertion(expr, response)) return false;
    }
    return true;
}

class ScenarioEngine::Impl {
public:
    explicit Impl(const std::string& config_path)
        : config_path_(config_path)
        , bucket_(100.0, 100.0) {}

    void stop() {
        stopped_ = true;
        std::lock_guard<std::mutex> lock(ioc_mutex_);
        if (active_ioc_) active_ioc_->stop();
    }

    void set_target_rps(uint32_t rps) {
        double rate = rps > 0 ? static_cast<double>(rps) : 100.0;
        target_rps_.store(rps > 0 ? rps : 100, std::memory_order_relaxed);
        bucket_.set_rate(rate);
        // A burst of at least 1s worth of tokens (and never below 100) lets the
        // engine start fast and absorb the wake-up clustering Windows timer
        // granularity (~15.6ms) causes when many workers sleep in lockstep.
        bucket_.set_burst(std::max(100.0, rate));
    }

    uint32_t target_rps() const { return target_rps_.load(std::memory_order_relaxed); }

    void set_max_duration(std::chrono::milliseconds duration) {
        max_duration_ms_.store(duration.count() > 0
            ? duration.count() : 0, std::memory_order_relaxed);
    }

    std::chrono::milliseconds max_duration() const {
        return std::chrono::milliseconds(
            max_duration_ms_.load(std::memory_order_relaxed));
    }

    void set_auth_provider(std::shared_ptr<security::AuthProvider> auth) {
        auth_ = std::move(auth);
    }

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
        // Keep the context alive for the whole run. Without a work guard the
        // io_context auto-stops when the last outstanding operation completes
        // (e.g. between slot-aligned worker requests); any async_request issued
        // on a stopped context is queued but never executed, so the worker's
        // run_one() loop spins until the stage deadline. The guard prevents the
        // implicit stop; run_one() then returns 0 only after an explicit stop().
        auto guard = boost::asio::make_work_guard(*ioc);
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

        uint32_t concurrency = core::kDefaultConcurrency;
        if (!cfg.load_profile.stages.empty()) {
            concurrency = cfg.load_profile.stages[0].concurrent_users;
        }

        metrics::MetricsCollector metrics;

        // Global cap on the whole test run (0 = disabled)
        auto max_ms = max_duration_ms_.load(std::memory_order_relaxed);
        auto global_end = (max_ms > 0)
            ? std::chrono::steady_clock::now() + std::chrono::milliseconds(max_ms)
            : std::chrono::steady_clock::time_point::max();

        for (size_t stage_idx = 0; stage_idx < cfg.load_profile.stages.size() && !stopped_; ++stage_idx) {
            const auto& stage = cfg.load_profile.stages[stage_idx];

            if (std::chrono::steady_clock::now() >= global_end) {
                stopped_ = true;
                std::lock_guard<std::mutex> lock(ioc_mutex_);
                if (active_ioc_) active_ioc_->stop();
                break;
            }

            // Update rate limiter for this stage
            if (stage.target_rps > 0) {
                set_target_rps(stage.target_rps);
            }

            uint32_t stage_concurrency = stage.concurrent_users > 0
                ? stage.concurrent_users : concurrency;

            // Worker deadline: explicit stage duration capped by the global
            // test cap. When both are absent the stage runs until stop().
            auto stage_end = std::chrono::steady_clock::time_point::max();
            if (stage.duration.count() > 0) {
                stage_end = std::chrono::steady_clock::now() + stage.duration;
            }
            if (global_end != std::chrono::steady_clock::time_point::max()) {
                stage_end = std::min(stage_end, global_end);
            }

            std::vector<std::thread> workers;
            for (uint32_t w = 0; w < stage_concurrency; ++w) {
                workers.emplace_back([this, ioc, &metrics, &callback, proto_name, cfg, tls_cfg, stage_end, stage_concurrency, w]() {
                    try {
                        auto client = net::ProtocolFactory::create(proto_name, *ioc, tls_cfg);
                        if (!client) {
                            {
                                std::lock_guard<std::mutex> lock(last_error_mtx_);
                                last_error_ = "unsupported protocol: " + proto_name;
                            }
                            return;
                        }

                        bool any_steps = false;
                        for (const auto& s : cfg.scenarios) {
                            if (!s.steps.empty()) { any_steps = true; break; }
                        }
                        if (!any_steps) return;

                        std::mt19937 rng(static_cast<uint32_t>(
                            std::chrono::steady_clock::now().time_since_epoch().count()
                            ^ std::hash<std::thread::id>{}(std::this_thread::get_id())));
                        size_t step_ix = 0;

                        // Sustained load: loop until the stage deadline (or an
                        // external stop) while the shared token bucket keeps the
                        // aggregate rate at the stage's target_rps.
                        //
                        // The loop is driven by absolute time slots, not by
                        // polling the bucket: polling with sleep_for(1ms) is
                        // useless on Windows where the timer granularity is
                        // ~15.6ms (it caps the rate far below target_rps). Each
                        // worker computes its own next slot as start + k*period
                        // (period = workers / rate) with a phase offset, sleeps
                        // the bulk of the interval, then spins the last 2ms for
                        // precision. The bucket still guards against over-rate
                        // bursts under scheduler jitter.
                        double rate = static_cast<double>(target_rps_.load(std::memory_order_relaxed));
                        if (rate <= 0.0) rate = 1.0;
                        auto slot_period = std::chrono::nanoseconds(static_cast<int64_t>(
                            stage_concurrency / rate * 1'000'000'000.0));
                        if (slot_period.count() <= 0) slot_period = std::chrono::nanoseconds(1);
                        auto next_slot = std::chrono::steady_clock::now() +
                            slot_period * static_cast<int64_t>(w) /
                            static_cast<int64_t>(stage_concurrency);
                        bool first_slot = true;

                        while (!stopped_) {
                            auto now = std::chrono::steady_clock::now();
                            if (now >= stage_end) break;

                            if (!first_slot) {
                                next_slot += slot_period;
                                if (now < next_slot) {
                                    auto t_sleep0 = std::chrono::steady_clock::now();
                                    auto sleep_dur = next_slot - now;
                                    if (sleep_dur > std::chrono::milliseconds(2)) {
                                        auto wake_at = next_slot - std::chrono::milliseconds(2);
                                        while (std::chrono::steady_clock::now() < wake_at && !stopped_) {
                                            auto rem = wake_at - std::chrono::steady_clock::now();
                                            std::this_thread::sleep_for(std::min<std::chrono::steady_clock::duration>(
                                                rem, std::chrono::milliseconds(10)));
                                        }
                                    }
                                    while (std::chrono::steady_clock::now() < next_slot && !stopped_) {
                                        // precise spin of the last few ms
                                    }
                                    now = std::chrono::steady_clock::now();
                                    if (now >= stage_end) break;
                                }
                            } else {
                                first_slot = false;
                            }

                            if (!bucket_.try_consume()) continue;

                            const Scenario* scenario =
                                pick_weighted_scenario(cfg.scenarios, rng);
                            if (!scenario || scenario->steps.empty()) continue;
                            const auto& step =
                                scenario->steps[step_ix % scenario->steps.size()];
                            ++step_ix;

                            net::Request req;
                            req.method = step.method;
                            req.path = step.path;
                            req.body = step.body;
                            req.headers = step.headers;
                            resolve_url(cfg.target.base_url, req.host, req.port,
                                       req.use_tls, step.use_tls);

                            if (auth_) {
                                if (auth_->is_expired()) {
                                    (void)auth_->refresh_token();
                                }
                                auth_->apply_headers(req.headers);
                            }

                            auto capture_req = std::make_shared<net::Request>(std::move(req));
                            auto done = std::make_shared<std::atomic<bool>>(false);
                            client->async_request(*capture_req,
                                [capture_req, &metrics, &callback, done, step](std::error_code ec, net::Response resp) mutable {
                                    uint16_t code = ec ? 0 : resp.status_code;
                                    if (code != 0 && !evaluate_assertions(step, resp)) {
                                        code = 0;
                                    }
                                    metrics.record_request(code, resp.latency,
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
                    } catch (const std::exception& e) {
                        std::lock_guard<std::mutex> lock(last_error_mtx_);
                        last_error_ = "worker thread error: " + std::string(e.what());
                    }
                });
            }

            // Wait for the stage deadline (or global cap if closer). Workers
            // exit by themselves once stage_end passes; stopping the ioc here
            // unblocks any worker still parked in run_one().
            bool externally_stopped = stopped_;
            if (stage_end != std::chrono::steady_clock::time_point::max() && !stopped_) {
                while (std::chrono::steady_clock::now() < stage_end && !stopped_) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                }
                externally_stopped = stopped_;
                stopped_ = true;
                std::lock_guard<std::mutex> lock(ioc_mutex_);
                if (active_ioc_) active_ioc_->stop();
            }

            for (auto& t : workers) {
                if (t.joinable()) t.join();
            }

            // Reset io_context for next stage. When the stop above was caused by
            // stage completion (not a user stop or the global cap), keep going.
            if (stage_idx + 1 < cfg.load_profile.stages.size() && !externally_stopped) {
                ioc->restart();
                stopped_ = false;
            }
        }

        {
            std::lock_guard<std::mutex> lock(ioc_mutex_);
            active_ioc_ = nullptr;
        }
        guard.reset();
    }

    bool check_sla(const metrics::MetricsCollector& m) const {
        if (m.error_rate() > config_.sla.max_error_rate) {
            return false;
        }
        auto snap = m.snapshot();
        if (snap.p99_latency_us >
            static_cast<uint64_t>(config_.sla.max_p99_latency.count()) * 1000) {
            return false;
        }
        return true;
    }

    std::string last_error() const {
        std::lock_guard<std::mutex> lock(last_error_mtx_);
        return last_error_;
    }

private:
    void resolve_url(const std::string& url, std::string& host,
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
    std::atomic<int64_t> max_duration_ms_{0};
    std::shared_ptr<security::AuthProvider> auth_;
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
void ScenarioEngine::set_max_duration(std::chrono::milliseconds duration) { impl_->set_max_duration(duration); }
std::chrono::milliseconds ScenarioEngine::max_duration() const { return impl_->max_duration(); }
void ScenarioEngine::set_auth_provider(std::shared_ptr<security::AuthProvider> auth) { impl_->set_auth_provider(std::move(auth)); }
bool ScenarioEngine::check_sla(const metrics::MetricsCollector& m) const { return impl_->check_sla(m); }
std::string ScenarioEngine::last_error() const { return impl_->last_error(); }

} // namespace cppload::scenario