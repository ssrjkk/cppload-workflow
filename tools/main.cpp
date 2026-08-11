// @author ssrjkk | cppload
#include "cppload/scenario/engine.hpp"
#include "cppload/metrics/collector.hpp"
#include "cppload/metrics/prometheus_exporter.hpp"
#include "cppload/security/auth_provider.hpp"
#include "cppload/vault/vault_client.hpp"
#include "cppload/otel/exporter.hpp"
#include "cppload/core/constants.hpp"
#include "cppload/core/token_bucket.hpp"
#include "cppload/core/url_parse.hpp"
#include "cppload/net/http_client.hpp"
#include <boost/asio/io_context.hpp>
#include <iostream>
#include <string>
#include <unordered_map>
#include <thread>
#include <atomic>
#include <mutex>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <memory>

using namespace cppload;

std::atomic<bool> g_stop_requested{false};

void signal_handler(int) {
    g_stop_requested = true;
}

struct CliArgs {
    std::string config;
    std::string target;
    std::string auth_type;
    std::string auth_token;
    std::string client_id;
    std::string client_secret;
    std::string token_endpoint;
    std::string vault_addr;
    std::string vault_token;
    std::string otlp_endpoint;
    int rps{100};
    int duration{60};
    bool duration_set{false};
    bool help{false};
    bool version{false};
    bool verbose{false};
    bool error{false};
};

CliArgs parse_args(int argc, char* argv[]) {
    CliArgs args;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--help" || arg == "-h") { args.help = true; break; }
        if (arg == "--version" || arg == "-v") { args.version = true; break; }
        auto eq = arg.find('=');
        std::string key = (eq != std::string::npos) ? arg.substr(0, eq) : arg;
        auto next = [&]() -> std::string {
            if (eq != std::string::npos) return arg.substr(eq + 1);
            if (i + 1 < argc) return argv[++i];
            return "";
        };
        if (key == "--config" || key == "-c") args.config = next();
        else if (key == "--target" || key == "-t") args.target = next();
        else if (key == "--rps" || key == "-r") {
            auto s = next();
            try { args.rps = std::stoi(s); }
            catch (const std::exception&) { std::cerr << "Invalid --rps: " << s << "\n"; args.error = true; }
        }
        else if (key == "--duration" || key == "-d") {
            auto s = next();
            args.duration_set = true;
            try { args.duration = std::stoi(s); }
            catch (const std::exception&) { std::cerr << "Invalid --duration: " << s << "\n"; args.error = true; }
        }
        else if (key == "--auth-type") args.auth_type = next();
        else if (key == "--auth-token") args.auth_token = next();
        else if (key == "--client-id") args.client_id = next();
        else if (key == "--client-secret") args.client_secret = next();
        else if (key == "--token-endpoint") args.token_endpoint = next();
        else if (key == "--vault-addr") args.vault_addr = next();
        else if (key == "--vault-token") args.vault_token = next();
        else if (key == "--otlp-endpoint") args.otlp_endpoint = next();
        else if (key == "--verbose") args.verbose = true;
        else if (key == "--help" || key == "-h") args.help = true;
        else if (key == "--version" || key == "-v") args.version = true;
        else {
            std::cerr << "Unknown option: " << key << "\n";
            args.error = true;
        }
    }
    return args;
}

void print_help(const char* prog) {
    std::cout << "cppload-pro CLI - Enterprise Load Testing\n"
        << "\nUsage: " << prog << " [options]\n"
        << "\nOptions:\n"
        << "  -h, --help                    Show help\n"
        << "  -v, --version                 Show version\n"
        << "  -c, --config=FILE             Config file path (YAML)\n"
        << "  -t, --target=URL              Target URL\n"
        << "  -r, --rps=N                   Requests per second (default: 100)\n"
        << "  -d, --duration=N              Duration in seconds (default: 60, 0 = unlimited)\n"
        << "  --auth-type=TYPE              Auth: none|bearer|apikey|oauth2\n"
        << "  --auth-token=TOKEN            Bearer token or API key\n"
        << "  --client-id=ID                OAuth2 client ID\n"
        << "  --client-secret=SECRET        OAuth2 client secret\n"
        << "  --token-endpoint=URL          OAuth2 token endpoint\n"
        << "  --vault-addr=URL              Vault server address\n"
        << "  --vault-token=TOKEN           Vault token\n"
        << "  --otlp-endpoint=URL           OTLP traces endpoint\n"
        << "  --verbose                     Verbose output (per-request)\n"
        << "\nExamples:\n"
        << "  " << prog << " --config=scenarios/ecommerce/load-test.yaml\n"
        << "  " << prog << " --target=http://localhost:8080 --rps=500 --duration=120\n"
        << "  " << prog << " --config=test.yaml --client-id=$ID --client-secret=$SEC\n";
}

security::AuthConfig build_auth_config(const CliArgs& args,
                                       const scenario::ScenarioConfig* cfg) {
    security::AuthConfig auth_cfg;
    if (!args.auth_type.empty()) {
        if (args.auth_type == "bearer" && !args.auth_token.empty()) {
            auth_cfg.type = security::AuthType::BEARER_TOKEN;
            auth_cfg.token = args.auth_token;
        } else if (args.auth_type == "apikey" && !args.auth_token.empty()) {
            auth_cfg.type = security::AuthType::API_KEY;
            auth_cfg.api_key = args.auth_token;
        } else if (args.auth_type == "oauth2") {
            auth_cfg.type = security::AuthType::OAUTH2;
            auth_cfg.client_id = args.client_id;
            auth_cfg.client_secret = args.client_secret;
            auth_cfg.token_endpoint = args.token_endpoint;
        }
    } else if (cfg && cfg->authentication.type == "oauth2") {
        auth_cfg.type = security::AuthType::OAUTH2;
        auth_cfg.client_id = cfg->authentication.client_credentials.client_id;
        auth_cfg.client_secret = cfg->authentication.client_credentials.client_secret;
        auth_cfg.token_endpoint = cfg->authentication.token_endpoint;
    }
    return auth_cfg;
}

int print_results(const metrics::MetricsCollector& metrics,
                  std::chrono::seconds elapsed,
                  const std::string& test_id) {
    auto m = metrics.snapshot();
    std::cout << "Results (" << elapsed.count() << "s):\n"
        << "  Test ID:       " << test_id << "\n"
        << "  Total requests: " << m.total_requests << "\n"
        << "  Successful:    " << m.successful_requests << "\n"
        << "  Failed:        " << m.failed_requests << "\n"
        << "  Error rate:    " << metrics.error_rate() << "%\n"
        << "  Mean latency:  " << m.mean_latency_us << " us\n"
        << "  P95 latency:   " << m.p95_latency_us << " us\n"
        << "  P99 latency:   " << m.p99_latency_us << " us\n"
        << "  Actual RPS:    " << metrics.requests_per_second() << "\n";
    return 0;
}

int run_direct(const CliArgs& args, security::AuthProvider& auth, otel::Tracer& tracer) {
    if (args.rps <= 0) {
        std::cerr << "Invalid --rps: must be > 0 (got " << args.rps << ")\n";
        return 1;
    }

    auto u = core::parse_url(args.target);

    uint16_t target_port = 0;
    {
        long parsed_port = 0;
        try {
            parsed_port = std::stol(u.port);
        } catch (const std::exception&) {
            std::cerr << "Invalid port in target URL: \"" << u.port << "\"\n";
            return 1;
        }
        if (parsed_port <= 0 || parsed_port > 65535) {
            std::cerr << "Port out of range in target URL: " << parsed_port << "\n";
            return 1;
        }
        target_port = static_cast<uint16_t>(parsed_port);
    }

    metrics::MetricsCollector metrics;
    boost::asio::io_context ioc;
    net::Http11Client client(ioc);
    client.set_timeout(core::kDefaultTimeout);
    TokenBucket bucket(static_cast<double>(args.rps), 1.0);

    std::cout << "cppload-pro Load Tester (direct mode)\n"
        << "Target: " << args.target << "\n"
        << "RPS: " << args.rps << "\n"
        << "Duration: " << args.duration << "s\n"
        << "---\n";

    auto test_start = std::chrono::steady_clock::now();
    auto end = args.duration > 0
        ? test_start + std::chrono::seconds(args.duration)
        : std::chrono::steady_clock::time_point::max();

    std::mutex cout_mtx;

    tracer.start_span("load_test");
    tracer.add_attribute("target", args.target);
    tracer.add_attribute("target_rps", std::to_string(args.rps));
    tracer.add_attribute("duration_s", std::to_string(args.duration));

    while (std::chrono::steady_clock::now() < end && !g_stop_requested) {
        if (!bucket.try_consume_for(std::chrono::milliseconds(10))) {
            continue;
        }

        net::Request req;
        req.method = "GET";
        req.path = u.path;
        req.host = u.host;
        req.port = target_port;
        req.use_tls = u.tls;
        auth.apply_headers(req.headers);

        auto capture_req = std::make_shared<net::Request>(std::move(req));
        auto done = std::make_shared<std::atomic<bool>>(false);
        client.async_request(*capture_req,
            [&, capture_req, done](std::error_code ec, net::Response resp) {
                metrics.record_request(resp.status_code, resp.latency,
                                       capture_req->body.size(), resp.body.size());
                if (args.verbose) {
                    std::lock_guard<std::mutex> lock(cout_mtx);
                    if (ec) {
                        std::cout << "ERR " << ec.message() << "\n";
                    } else {
                        std::cout << resp.status_code << " " << resp.latency.count() << "us\n";
                    }
                }
                tracer.start_span("GET " + u.path);
                tracer.add_attribute("http.method", "GET");
                tracer.add_attribute("http.url", u.path);
                tracer.add_attribute("http.status_code", std::to_string(resp.status_code));
                tracer.add_attribute("http.latency_us", std::to_string(resp.latency.count()));
                tracer.end_span();
                done->store(true, std::memory_order_release);
            });

        while (!done->load(std::memory_order_acquire) && !g_stop_requested) {
            ioc.run_one();
        }
    }

    g_stop_requested = true;
    // Drain any remaining async completions so no handler outlives this scope.
    ioc.restart();
    ioc.run();
    tracer.end_span();

    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::steady_clock::now() - test_start);
    return print_results(metrics, elapsed, "direct");
}

int main(int argc, char* argv[]) {
    auto args = parse_args(argc, argv);

    if (args.error) {
        std::cerr << "Run '" << argv[0] << " --help' for usage.\n";
        return 1;
    }
    if (args.help) { print_help(argv[0]); return 0; }
    if (args.version) {
        std::cout << "cppload-pro " << core::kVersion << "\n";
        return 0;
    }

    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    // --- Scenario engine ---
    std::unique_ptr<scenario::ScenarioEngine> engine;

    if (!args.config.empty()) {
        engine = std::make_unique<scenario::ScenarioEngine>(args.config);
        if (!engine->load_config()) {
            std::cerr << "Config error: " << engine->last_error() << "\n";
            return 1;
        }
        if (!engine->validate()) {
            std::cerr << "Validation error: " << engine->last_error() << "\n";
            return 1;
        }
    } else if (args.target.empty()) {
        std::cerr << "Either --config or --target is required\n";
        print_help(argv[0]);
        return 1;
    }

    // --- Auth ---
    security::AuthConfig auth_cfg = build_auth_config(
        args, engine ? &engine->config() : nullptr);
    std::shared_ptr<security::AuthProvider> auth;
    if (auth_cfg.type != security::AuthType::NONE) {
        try {
            auth = std::make_shared<security::AuthProvider>(auth_cfg);
        } catch (const std::exception& e) {
            std::cerr << "Auth setup failed: " << e.what() << "\n";
            return 1;
        }
    }

    // --- Vault ---
    std::unique_ptr<vault::VaultClient> vault;
    if (!args.vault_addr.empty()) {
        vault::VaultConfig vc;
        vc.address = args.vault_addr;
        vc.token = args.vault_token;
        vault = std::make_unique<vault::VaultClient>(vc);
        if (vault->is_connected()) {
            std::cout << "Vault connected: " << vc.address << "\n";
        } else {
            std::cout << "Vault not available\n";
        }
    }

    // --- OTLP ---
    otel::TraceConfig tc;
    if (!args.otlp_endpoint.empty()) tc.endpoint = args.otlp_endpoint;
    otel::Tracer tracer(tc);

    // --- Direct (--target) mode ---
    if (!engine) {
        security::AuthProvider noop;
        return run_direct(args, auth ? *auth : noop, tracer);
    }

    // --- Apply settings ---
    if (args.rps > 0) {
        engine->set_target_rps(static_cast<uint32_t>(args.rps));
    }
    if (args.duration_set && args.duration > 0) {
        engine->set_max_duration(std::chrono::seconds(args.duration));
    }
    if (auth) {
        engine->set_auth_provider(auth);
    }

    std::cout << "cppload-pro Load Tester\n"
        << "Target: " << engine->config().target.base_url << "\n"
        << "RPS: " << args.rps << "\n"
        << "Duration: " << args.duration << "s\n"
        << "---\n";

    auto test_start = std::chrono::steady_clock::now();

    tracer.start_span("load_test");
    tracer.add_attribute("test_id", engine->config().test_id);
    tracer.add_attribute("target", engine->config().target.base_url);
    tracer.add_attribute("target_rps", std::to_string(args.rps));
    tracer.add_attribute("duration_s", std::to_string(args.duration));

    metrics::MetricsCollector metrics;

    // --- Optional Prometheus exporter (from config observability section) ---
    metrics::PrometheusExporter prometheus(
        "0.0.0.0:" + std::to_string(engine->config().observability.metrics.prometheus.port));
    if (engine->config().observability.metrics.prometheus.enabled) {
        if (prometheus.start()) {
            std::cout << "Prometheus exporter: " << prometheus.endpoint() << "\n";
        } else {
            std::cout << "Prometheus exporter: failed to start\n";
        }
    }

    std::thread watcher([&]() {
        auto last_update = std::chrono::steady_clock::now();
        while (!g_stop_requested) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            auto now = std::chrono::steady_clock::now();
            if (prometheus.is_running() &&
                now - last_update >= std::chrono::seconds(1)) {
                prometheus.update_metrics(metrics);
                last_update = now;
            }
        }
        engine->stop();
    });

    try {
        engine->run([&](const scenario::HttpStep& step,
                         const net::Response& resp,
                         metrics::MetricsCollector& /* m */) {
            // Record into the CLI collector (assertion-adjusted, matching the
            // engine's own accounting) so results/SLA/Prometheus reflect real
            // measurements instead of an empty collector.
            uint16_t code = resp.status_code;
            if (code != 0 && !scenario::evaluate_assertions(step, resp)) {
                code = 0;
            }
            metrics.record_request(code, resp.latency,
                                   step.body.size(), resp.body.size());
            tracer.start_span(step.method + " " + step.path);
            tracer.add_attribute("http.method", step.method);
            tracer.add_attribute("http.url", step.path);
            tracer.add_attribute("http.status_code", std::to_string(resp.status_code));
            tracer.add_attribute("http.latency_us", std::to_string(resp.latency.count()));
            tracer.end_span();
        });
    } catch (const std::exception& e) {
        std::cerr << "Load test failed: " << e.what() << "\n";
        g_stop_requested = true;
        if (watcher.joinable()) watcher.join();
        return 1;
    }

    g_stop_requested = true;
    if (watcher.joinable()) watcher.join();

    tracer.end_span();

    if (prometheus.is_running()) {
        prometheus.update_metrics(metrics);
        prometheus.stop();
    }

    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::steady_clock::now() - test_start);

    // --- Results ---
    print_results(metrics, elapsed, engine->config().test_id);

    bool sla_ok = engine->check_sla(metrics);
    std::cout << "\nSLA: " << (sla_ok ? "PASSED" : "FAILED") << "\n";

    return sla_ok ? 0 : 2;
}
