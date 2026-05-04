#include <boost/asio/io_context.hpp>
#include <iostream>
#include <thread>
#include <atomic>
#include "cppload/net/http_client.hpp"
#include "cppload/metrics/collector.hpp"

std::atomic<bool> running{true};

void signal_handler(int) {
    running = false;
}

int main(int argc, char* argv[]) {
    boost::asio::io_context ioc;
    cppload::net::HttpClient client(ioc);
    cppload::metrics::MetricsCollector metrics;
    
    std::cout << "cppload-pro HTTP Worker starting...\n";
    
    boost::asio::signal_set signals(ioc, SIGINT, SIGTERM);
    signals.async_wait([](auto, int) { running = false; });
    
    std::thread worker([&ioc]() {
        ioc.run();
    });
    
    while (running) {
        cppload::net::HttpRequest req;
        req.method = "GET";
        req.target = "/";
        req.host = "localhost";
        req.port = "8080";
        
        client.async_request(req, [&metrics](const auto& resp) {
            metrics.record_request(resp.status_code, resp.latency, 0, resp.body.size());
        });
        
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    
    std::cout << "Shutting down... RPS: " << metrics.requests_per_second() << "\n";
    
    ioc.stop();
    worker.join();
    return 0;
}
