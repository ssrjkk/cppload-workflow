#include <boost/asio.hpp>
#include <boost/beast/core.hpp>
#include <iostream>
#include <thread>
#include <atomic>
#include <chrono>
#include <csignal>
#include <string>
#include "cppload/net/http_client.hpp"
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
};

void print_usage(const char* prog) {
    std::cerr << "Usage: " << prog << " [options]\n"
        << "  --host HOST       Target host (default: localhost)\n"
        << "  --port PORT       Target port (default: 8080)\n"
        << "  --path PATH       Target path (default: /)\n"
        << "  --method METHOD   HTTP method (default: GET)\n"
        << "  --rps N           Requests per second (default: 100)\n"
        << "  --duration N      Test duration in seconds (default: 60)\n";
}

WorkerConfig parse_args(int argc, char* argv[]) {
    WorkerConfig cfg;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--host" && i + 1 < argc) cfg.target_host = argv[++i];
        else if (arg == "--port" && i + 1 < argc) cfg.target_port = argv[++i];
        else if (arg == "--path" && i + 1 < argc) cfg.target_path = argv[++i];
        else if (arg == "--method" && i + 1 < argc) cfg.method = argv[++i];
        else if (arg == "--rps" && i + 1 < argc) cfg.rps = static_cast<uint32_t>(std::stoi(argv[++i]));
        else if (arg == "--duration" && i + 1 < argc) cfg.duration_sec = std::stoi(argv[++i]);
        else if (arg == "--help") { print_usage(argv[0]); exit(0); }
    }
    return cfg;
}

int main(int argc, char* argv[]) {
    auto cfg = parse_args(argc, argv);

    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    asio::io_context ioc;
    net::HttpClient client(ioc);
    cppload::metrics::MetricsCollector metrics;
    cppload::TokenBucket bucket(static_cast<double>(cfg.rps));

    std::cout << "cppload-pro HTTP Worker\n"
        << "Target: " << cfg.target_host << ":" << cfg.target_port << cfg.target_path << "\n"
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
                << " p99=" << metrics.p99_latency_us() << "us"
                << "\n";
        }
    });

    // IO thread
    std::thread worker([&ioc]() { ioc.run(); });

    // Main load loop
    while (running && std::chrono::steady_clock::now() < end_time) {
        net::HttpRequest req;
        req.method = cfg.method;
        req.target = cfg.target_path;
        req.host = cfg.target_host;
        req.port = cfg.target_port;

        client.async_request(req, [&metrics](const auto& resp) {
            metrics.record_request(
                resp.status_code, resp.latency,
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
        << "  P95 latency: " << metrics.p95_latency_us() << "us\n"
        << "  P99 latency: " << metrics.p99_latency_us() << "us\n"
        << "  Actual RPS: " << metrics.requests_per_second() << "\n";

    return 0;
}
