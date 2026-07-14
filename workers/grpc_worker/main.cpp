#include <iostream>
#include <string>
#include <memory>
#include <thread>
#include <atomic>
#include <chrono>
#include <csignal>
#include <grpcpp/grpcpp.h>
#include <boost/asio.hpp>
#include "cppload/net/http_client.hpp"
#include "cppload/core/token_bucket.hpp"
#include "cppload/metrics/collector.hpp"
#include "cppload/scenario/engine.hpp"
#include "load_controller.pb.h"
#include "load_controller.grpc.pb.h"

using grpc::Channel;
using grpc::ClientContext;
using grpc::Status;
using cppload::LoadController;
using cppload::RegisterWorkerRequest;
using cppload::RegisterWorkerResponse;
using cppload::AssignTaskRequest;
using cppload::AssignTaskResponse;
using cppload::MetricsReport;
using cppload::Ack;

std::atomic<bool> running{true};
std::atomic<int64_t> total_requests{0};
std::atomic<int64_t> successful_requests{0};
std::atomic<int64_t> failed_requests{0};

void signal_handler(int) {
    running = false;
}

class GrpcWorker {
public:
    GrpcWorker(const std::string& controller_address, const std::string& worker_id, int32_t max_rps)
        : worker_id_(worker_id), max_rps_(max_rps), worker_id_assigned_(0)
    {
        stub_ = LoadController::NewStub(grpc::CreateChannel(
            controller_address, grpc::InsecureChannelCredentials()));
    }

    bool register_worker() {
        RegisterWorkerRequest request;
        request.set_worker_id(worker_id_);
        request.set_worker_address("localhost:50051");
        request.set_max_rps(max_rps_);

        RegisterWorkerResponse response;
        ClientContext context;

        Status status = stub_->RegisterWorker(&context, request, &response);
        if (status.ok() && response.accepted()) {
            worker_id_assigned_ = response.assigned_worker_id();
            std::cout << "Worker registered: " << response.controller_id()
                      << " (id=" << worker_id_assigned_ << ")\n";
            return true;
        }
        std::cerr << "Registration failed: " << status.error_message() << "\n";
        return false;
    }

    bool assign_task(AssignTaskResponse& task) {
        AssignTaskRequest request;
        request.set_worker_id(worker_id_assigned_);

        ClientContext context;
        Status status = stub_->AssignTask(&context, request, &task);
        return status.ok() && task.has_task();
    }

    void report_metrics() {
        ClientContext context;
        context.set_deadline(std::chrono::system_clock::now() +
            std::chrono::seconds(10));
        Ack ack;
        auto writer = stub_->ReportMetrics(&context, &ack);

        while (running) {
            std::this_thread::sleep_for(std::chrono::seconds(5));

            MetricsReport report;
            report.set_worker_id(worker_id_assigned_);
            report.set_timestamp_ms(std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count());
            report.set_total_requests(total_requests.load());
            report.set_successful_requests(successful_requests.load());
            report.set_failed_requests(failed_requests.load());
            auto total = total_requests.load();
            report.set_error_rate(total > 0
                ? static_cast<double>(failed_requests.load()) / total * 100.0
                : 0.0);

            if (!writer->Write(report)) break;
        }
        writer->WritesDone();
        writer->Finish();
    }

    void unregister(const std::string& reason) {
        cppload::UnregisterWorkerRequest request;
        request.set_worker_id(worker_id_assigned_);
        request.set_reason(reason);

        Ack response;
        ClientContext context;
        stub_->UnregisterWorker(&context, request, &response);
    }

    int32_t assigned_id() const { return worker_id_assigned_; }

private:
    std::unique_ptr<LoadController::Stub> stub_;
    std::string worker_id_;
    int32_t max_rps_;
    int32_t worker_id_assigned_;
};

void run_load_test(const AssignTaskResponse& task) {
    boost::asio::io_context ioc;
    cppload::net::Http11Client client(ioc);
    cppload::TokenBucket bucket(static_cast<double>(task.task().target_rps()));
    cppload::metrics::MetricsCollector metrics;

    auto end_time = std::chrono::steady_clock::now()
        + std::chrono::seconds(task.task().duration_seconds());

    while (running && std::chrono::steady_clock::now() < end_time) {
        cppload::net::Request req;
        req.method = "GET";
        std::string url = task.task().target_url();
        req.path = "/";
        req.host = url;
        req.port = 80;

        // Parse URL to extract host, port and path
        auto proto_end = url.find("://");
        auto url_start = (proto_end != std::string::npos) ? proto_end + 3 : 0;
        auto path_start = url.find("/", url_start);
        auto host_port_end = (path_start != std::string::npos) ? path_start : url.size();
        std::string host_port = url.substr(url_start, host_port_end - url_start);
        auto colon = host_port.find(":");
        if (colon != std::string::npos) {
            req.host = host_port.substr(0, colon);
            try {
                auto p = std::stoul(host_port.substr(colon + 1));
                if (p > 0 && p <= 65535) req.port = static_cast<uint16_t>(p);
            } catch (const std::exception&) {}
        } else {
            req.host = host_port;
        }
        if (path_start != std::string::npos) {
            req.path = url.substr(path_start);
        }

        for (const auto& [key, val] : task.task().headers()) {
            req.headers[key] = val;
        }

        auto start = std::chrono::steady_clock::now();
        std::atomic<bool> done{false};
        size_t req_body_size = req.body.size();

        client.async_request(req, [start, &metrics, req_body_size, &done](std::error_code, cppload::net::Response resp) {
            auto latency = std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::steady_clock::now() - start);
            metrics.record_request(resp.status_code, latency, req_body_size, resp.body.size());
            total_requests.fetch_add(1);
            if (resp.status_code >= 200 && resp.status_code < 400) {
                successful_requests.fetch_add(1);
            } else {
                failed_requests.fetch_add(1);
            }
            done = true;
        });
            total_requests.fetch_add(1);
            if (resp.status_code >= 200 && resp.status_code < 400) {
                successful_requests.fetch_add(1);
            } else {
                failed_requests.fetch_add(1);
            }
            done = true;
        });

        if (!done) ioc.run_one();

        bucket.consume();
    }
}

int main(int argc, char** argv) {
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    std::string controller_addr = "localhost:5000";
    std::string worker_id = "worker-1";
    int32_t max_rps = 5000;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--controller" && i + 1 < argc) controller_addr = argv[++i];
        else if (arg == "--worker-id" && i + 1 < argc) worker_id = argv[++i];
        else if (arg == "--max-rps" && i + 1 < argc) {
            try { max_rps = std::stoi(argv[++i]); }
            catch (const std::exception&) { std::cerr << "Invalid --max-rps value\n"; return 1; }
        }
        else if (arg == "--help") {
            std::cout << "Usage: grpc_worker --controller=HOST:PORT --worker-id=ID --max-rps=N\n";
            return 0;
        }
    }

    std::cout << "cppload-pro gRPC Worker starting...\n"
              << "Controller: " << controller_addr << "\n"
              << "Worker ID: " << worker_id << "\n"
              << "Max RPS: " << max_rps << "\n\n";

    GrpcWorker worker(controller_addr, worker_id, max_rps);

    if (!worker.register_worker()) {
        std::cerr << "Failed to register with controller\n";
        return 1;
    }

    std::thread reporter([&]() { worker.report_metrics(); });

    while (running) {
        AssignTaskResponse task;
        if (!worker.assign_task(task)) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
            continue;
        }

        std::cout << "Task assigned: " << task.task().scenario_name()
                  << " -> " << task.task().target_url()
                  << " (" << task.task().target_rps() << " RPS, "
                  << task.task().duration_seconds() << "s)\n";

        run_load_test(task);
    }

    running = false;
    reporter.join();
    worker.unregister("shutdown");

    std::cout << "Worker shutdown complete\n";
    return 0;
}
