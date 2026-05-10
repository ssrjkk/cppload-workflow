#include "cppload/scenario/engine.hpp"
#include "cppload/metrics/collector.hpp"
#include "cppload/security/auth_provider.hpp"
#include "cppload/vault/vault_client.hpp"
#include "cppload/otel/exporter.hpp"
#include <iostream>
#include <string>
#include <unordered_map>
#include <thread>
#include <atomic>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <memory>

using namespace cppload;

scenario::ScenarioEngine* volatile global_engine = nullptr;

void signal_handler(int) {
    if (global_engine) global_engine->stop();
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
    bool help{false};
    bool version{false};
    bool verbose{false};
};

CliArgs parse_args(int argc, char* argv[]) {
    CliArgs args;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--help" || arg == "-h") { args.help = true; break; }
        if (arg == "--version" || arg == "-v") { args.version = true; break; }
        auto eq = arg.find('=');
        auto next = [&]() -> std::string {
            if (eq != std::string::npos) return arg.substr(eq + 1);
            if (i + 1 < argc) return argv[++i];
            return "";
        };
        if (arg == "--config" || arg == "-c") args.config = next();
        else if (arg == "--target" || arg == "-t") args.target = next();
        else if (arg == "--rps" || arg == "-r") {
            auto s = next();
            try { args.rps = std::stoi(s); }
            catch (...) { std::cerr << "Invalid --rps: " << s << "\n"; args.help = true; }
        }
        else if (arg == "--duration" || arg == "-d") {
            auto s = next();
            try { args.duration = std::stoi(s); }
            catch (...) { std::cerr << "Invalid --duration: " << s << "\n"; args.help = true; }
        }
        else if (arg == "--auth-type") args.auth_type = next();
        else if (arg == "--auth-token") args.auth_token = next();
        else if (arg == "--client-id") args.client_id = next();
        else if (arg == "--client-secret") args.client_secret = next();
        else if (arg == "--token-endpoint") args.token_endpoint = next();
        else if (arg == "--vault-addr") args.vault_addr = next();
        else if (arg == "--vault-token") args.vault_token = next();
        else if (arg == "--otlp-endpoint") args.otlp_endpoint = next();
        else if (arg == "--verbose") args.verbose = true;
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
        << "  -d, --duration=N              Duration in seconds (default: 60)\n"
        << "  --auth-type=TYPE              Auth: none|bearer|oauth2\n"
        << "  --auth-token=TOKEN            Bearer token or API key\n"
        << "  --client-id=ID                OAuth2 client ID\n"
        << "  --client-secret=SECRET        OAuth2 client secret\n"
        << "  --token-endpoint=URL          OAuth2 token endpoint\n"
        << "  --vault-addr=URL              Vault server address\n"
        << "  --vault-token=TOKEN           Vault token\n"
        << "  --otlp-endpoint=URL           OTLP traces endpoint\n"
        << "  --verbose                     Verbose output\n"
        << "\nExamples:\n"
        << "  " << prog << " --config=scenarios/ecommerce/load-test.yaml\n"
        << "  " << prog << " --target=http://localhost:8080 --rps=500 --duration=120\n"
        << "  " << prog << " --config=test.yaml --client-id=$ID --client-secret=$SEC\n";
}

int main(int argc, char* argv[]) {
    auto args = parse_args(argc, argv);

    if (args.help) { print_help(argv[0]); return 0; }
    if (args.version) { std::cout << "cppload-pro 1.0.0\n"; return 0; }

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
    } else if (!args.target.empty()) {
        std::cerr << "Use --config for scenario-driven tests\n";
        return 1;
    } else {
        std::cerr << "Either --config or --target is required\n";
        print_help(argv[0]);
        return 1;
    }

    // --- OAuth2 ---
    security::AuthConfig auth_cfg;
    if (!args.auth_type.empty()) {
        if (args.auth_type == "bearer" && !args.auth_token.empty()) {
            auth_cfg.type = security::AuthType::BEARER_TOKEN;
            auth_cfg.token = args.auth_token;
        } else if (args.auth_type == "oauth2") {
            auth_cfg.type = security::AuthType::OAUTH2;
            auth_cfg.client_id = args.client_id;
            auth_cfg.client_secret = args.client_secret;
            auth_cfg.token_endpoint = args.token_endpoint;
        }
    }
    security::AuthProvider auth(auth_cfg);

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

    // --- Apply settings ---
    engine->set_target_rps(static_cast<uint32_t>(args.rps));

    // --- Start test ---
    std::cout << "cppload-pro Load Tester\n"
        << "Target: " << engine->config().target.base_url << "\n"
        << "RPS: " << args.rps << "\n"
        << "Duration: " << args.duration << "s\n"
        << "---\n";

    auto test_start = std::chrono::steady_clock::now();

    tracer.start_span("load_test");
    tracer.add_attribute("test_id", engine->config().test_id);
    tracer.add_attribute("target_rps", std::to_string(args.rps));
    tracer.add_attribute("duration_s", std::to_string(args.duration));

    metrics::MetricsCollector metrics;

    global_engine = engine.get();
    engine->run([&](const scenario::HttpStep& step,
                     const net::HttpResponse& resp,
                     metrics::MetricsCollector& m) {
        tracer.start_span(step.method + " " + step.path);
        tracer.add_attribute("http.method", step.method);
        tracer.add_attribute("http.url", step.path);
        tracer.add_attribute("http.status_code", std::to_string(resp.status_code));
        tracer.add_attribute("http.latency_us", std::to_string(resp.latency.count()));
        tracer.end_span();
    });

    tracer.end_span();
    global_engine = nullptr;

    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::steady_clock::now() - test_start);

    // --- Results ---
    auto m = metrics.snapshot();
    std::cout << "Results (" << elapsed.count() << "s):\n"
        << "  Test ID:       " << engine->config().test_id << "\n"
        << "  Total requests: " << m.total_requests << "\n"
        << "  Successful:    " << m.successful_requests << "\n"
        << "  Failed:        " << m.failed_requests << "\n"
        << "  Error rate:    " << metrics.error_rate() << "%\n"
        << "  Mean latency:  " << m.mean_latency_us << " us\n"
        << "  P95 latency:   " << metrics.p95_latency_us() << " us\n"
        << "  P99 latency:   " << metrics.p99_latency_us() << " us\n"
        << "  Actual RPS:    " << metrics.requests_per_second() << "\n";

    if (engine->check_sla(metrics)) {
        std::cout << "\nSLA: PASSED\n";
    } else {
        std::cout << "\nSLA: FAILED\n";
    }

    return 0;
}
