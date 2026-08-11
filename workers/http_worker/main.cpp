// @author ssrjkk | cppload
#include <boost/asio.hpp>
#include <boost/beast/core.hpp>
#include <iostream>
#include <thread>
#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <string>
#include "cppload/net/http_client.hpp"
#include "cppload/security/tls_context.hpp"
#include "cppload/core/token_bucket.hpp"
#include "cppload/metrics/collector.hpp"

namespace asio = boost::asio;
namespace net = cppload::net;

std::atomic<bool> running{true};

void signal_handler(int) {
    running = false;
}

struct WorkerConfig {
    std::string target_host{"localhost"};
    std::string target_port{"8080"};
    std::string target_path{"/"};
    std::string method{"GET"};
    uint32_t rps{100};
    int duration_sec{60};
    int report_interval_sec{5};
    bool use_tls{false};
    bool tls_verify{true};
    bool show_help{false};
};

void print_usage(const char* prog) {
    std::cerr << "Usage: " << prog << " [options]\n"
        << "  --host HOST       Target host (default: localhost)\n"
        << "  --port PORT       Target port (default: 8080)\n"
        << "  --path PATH       Target path (default: /)\n"
        << "  --method METHOD   HTTP method (default: GET)\n"
        << "  --rps N           Requests per second (default: 100)\n"
        << "  --duration N      Test duration in seconds (default: 60)\n"
        << "  --use-tls         Use HTTPS\n"
        << "  --tls-verify BOOL Verify TLS certificate (default: true)\n"
        << "  --help            Show this help\n";
}

WorkerConfig parse_args(int argc, char* argv[]) {
    WorkerConfig cfg;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        auto eq = arg.find('=');
        std::string key = arg;
        std::string value;
        bool inline_value = false;
        if (eq != std::string::npos) {
            key = arg.substr(0, eq);
            value = arg.substr(eq + 1);
            inline_value = true;
        }
        auto next_value = [&](const char* name) -> std::string {
            if (inline_value) return value;
            if (i + 1 < argc) return argv[++i];
            std::cerr << "Missing value for " << name << "\n";
            exit(1);
        };
        auto to_bool = [](const std::string& s) -> bool {
            return !(s == "0" || s == "false" || s == "off" || s == "no");
        };

        if (key == "--host") cfg.target_host = next_value("--host");
        else if (key == "--port") cfg.target_port = next_value("--port");
        else if (key == "--path") cfg.target_path = next_value("--path");
        else if (key == "--method") cfg.method = next_value("--method");
        else if (key == "--rps") {
            try { cfg.rps = static_cast<uint32_t>(std::stoi(next_value("--rps"))); }
            catch (const std::exception&) { std::cerr << "Invalid --rps value\n"; exit(1); }
        }
        else if (key == "--duration") {
            try { cfg.duration_sec = std::stoi(next_value("--duration")); }
            catch (const std::exception&) { std::cerr << "Invalid --duration value\n"; exit(1); }
        }
        else if (key == "--report-interval") {
            try { cfg.report_interval_sec = std::stoi(next_value("--report-interval")); }
            catch (const std::exception&) { std::cerr << "Invalid --report-interval value\n"; exit(1); }
        }
        else if (key == "--use-tls" || key == "--tls") {
            cfg.use_tls = inline_value ? to_bool(value) : true;
        }
        else if (key == "--tls-verify") {
            cfg.tls_verify = to_bool(next_value("--tls-verify"));
        }
        else if (key == "--help") { cfg.show_help = true; }
        else {
            std::cerr << "Unknown option: " << key << "\n";
            cfg.show_help = true;
        }
    }
    return cfg;
}

int main(int argc, char* argv[]) {
    auto cfg = parse_args(argc, argv);
    if (cfg.show_help) { print_usage(argv[0]); return 0; }
    if (cfg.rps == 0) {
        std::cerr << "Invalid --rps: must be > 0\n";
        return 1;
    }

    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    asio::io_context ioc;
    cppload::security::TlsConfig tls_cfg;
    tls_cfg.verify_peer = cfg.tls_verify;
    net::Http11Client client(ioc, tls_cfg);
    cppload::metrics::MetricsCollector metrics;
    cppload::TokenBucket bucket(static_cast<double>(cfg.rps));

    std::cout << "cppload-pro HTTP Worker\n"
        << "Target: " << (cfg.use_tls ? "https://" : "http://")
        << cfg.target_host << ":" << cfg.target_port << cfg.target_path << "\n"
        << "Method: " << cfg.method << "\n"
        << "RPS: " << cfg.rps << "\n"
        << "Duration: " << cfg.duration_sec << "s\n"
        << "---\n";

    auto start_time = std::chrono::steady_clock::now();
    auto end_time = start_time + std::chrono::seconds(cfg.duration_sec);

    // Report thread
    std::thread reporter([&]() {
        while (running) {
            std::this_thread::sleep_for(std::chrono::seconds(cfg.report_interval_sec));
            if (!running) break;
            auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::steady_clock::now() - start_time).count();
            auto m = metrics.snapshot();
            std::cout << "[" << elapsed << "s] "
                << "req=" << m.total_requests
                << " err=" << metrics.error_rate() << "%"
                << " rps=" << metrics.requests_per_second()
                << " p99=" << m.p99_latency_us << "us"
                << "\n";
        }
    });

    // IO thread
    std::thread worker([&ioc]() { ioc.run(); });

    // Main load loop
    while (running && std::chrono::steady_clock::now() < end_time) {
        net::Request req;
        req.method = cfg.method;
        req.path = cfg.target_path;
        req.host = cfg.target_host;
        req.use_tls = cfg.use_tls;
        try {
            auto p = std::stoul(cfg.target_port);
            req.port = (p > 0 && p <= 65535) ? static_cast<uint16_t>(p) : 80;
        } catch (const std::exception&) { req.port = 80; }

        client.async_request(req, [&metrics](std::error_code, net::Response resp) {
            metrics.record_request(
                static_cast<uint16_t>(resp.status_code), resp.latency,
                0, resp.body.size());
        });

        bucket.consume();
    }

    running = false;
    ioc.stop();
    worker.join();
    reporter.join();

    // Final report
    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::steady_clock::now() - start_time).count();
    auto m = metrics.snapshot();
    std::cout << "---\n"
        << "Results (" << elapsed << "s):\n"
        << "  Total requests: " << m.total_requests << "\n"
        << "  Successful: " << m.successful_requests << "\n"
        << "  Failed: " << m.failed_requests << "\n"
        << "  Error rate: " << metrics.error_rate() << "%\n"
        << "  Mean latency: " << m.mean_latency_us << "us\n"
        << "  P95 latency: " << m.p95_latency_us << "us\n"
        << "  P99 latency: " << m.p99_latency_us << "us\n"
        << "  Actual RPS: " << metrics.requests_per_second() << "\n";

    return 0;
}
