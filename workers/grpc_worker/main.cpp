#include <iostream>
#include <string>
#include <memory>
#include <grpcpp/grpcpp.h>
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

class GrpcWorker {
public:
    GrpcWorker(const std::string& controller_address) 
        : stub_(LoadController::NewStub(grpc::CreateChannel(
              controller_address, grpc::InsecureChannelCredentials()))) {}
    
    bool register_worker(const std::string& worker_id, int32_t max_rps) {
        RegisterWorkerRequest request;
        request.set_worker_id(worker_id);
        request.set_worker_address("localhost:50051");
        request.set_max_rps(max_rps);
        
        RegisterWorkerResponse response;
        ClientContext context;
        
        Status status = stub_->RegisterWorker(&context, request, &response);
        
        if (status.ok() && response.accepted()) {
            std::cout << "Worker registered: " << response.controller_id() << "\n";
            return true;
        }
        return false;
    }
    
    void run_load_test(const std::string& scenario, 
                       const std::string& target,
                       int32_t rps, int32_t duration_sec) {
        // Execute load test using C++ core
        std::cout << "Running load test: " << scenario << "\n";
        std::cout << "Target: " << target << "\n";
        std::cout << "RPS: " << rps << " for " << duration_sec << "s\n";
        
        // Report metrics periodically
        report_metrics();
    }
    
    void report_metrics() {
        ClientContext context;
        auto writer = stub_->ReportMetrics(&context, &ack_);
        
        // Generate and send metrics
        MetricsReport report;
        report.set_worker_id(1);
        report.set_timestamp_ms(std::chrono::system_clock::now().time_since_epoch().count() / 1000000);
        report.set_total_requests(1000);
        report.set_successful_requests(990);
        report.set_failed_requests(10);
        report.set_error_rate(1.0);
        
        writer->Write(report);
        writer->WritesDone();
        
        Ack response;
        writer->Finish(&response);
    }
    
    void unregister(const std::string& reason) {
        cppload::UnregisterWorkerRequest request;
        request.set_worker_id(1);
        request.set_reason(reason);
        
        Ack response;
        ClientContext context;
        stub_->UnregisterWorker(&context, request, &response);
    }
    
private:
    std::unique_ptr<LoadController::Stub> stub_;
    Ack ack_;
};

int main(int argc, char** argv) {
    std::cout << "cppload-pro gRPC Worker starting...\n";
    
    GrpcWorker worker("localhost:5000");
    
    if (!worker.register_worker("worker-1", 5000)) {
        std::cerr << "Failed to register worker\n";
        return 1;
    }
    
    worker.run_load_test("checkout_flow", "http://target:8080", 5000, 300);
    
    worker.unregister("test_completed");
    
    return 0;
}
