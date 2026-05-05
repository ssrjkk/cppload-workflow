#include "cppload/otel/exporter.hpp"
#include <string>
#include <chrono>
#include <stdexcept>

namespace cppload::otel {

class Tracer::Impl {
public:
    explicit Impl(const TraceConfig& config) 
        : config_(config), span_active_(false) {
        
        // In production, initialize OpenTelemetry SDK here:
        // - Create OTLP exporter
        // - Set up tracer provider
        // - Configure sampling
        
        generate_trace_id();
    }
    
    void start_span(const std::string& name) {
        if (span_active_) {
            end_span();  // Auto-end previous span
        }
        
        span_name_ = name;
        span_active_ = true;
        span_start_ = std::chrono::steady_clock::now();
        
        // In production: 
        // auto span = tracer_->StartSpan(name);
        // span->SetAttribute("service.name", config_.service_name);
    }
    
    void end_span() {
        if (!span_active_) return;
        
        auto now = std::chrono::steady_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(
            now - span_start_);
        
        // In production: export span via OTLP
        // span->End();
        // exporter_->Export(span);
        
        span_active_ = false;
    }
    
    void add_attribute(const std::string& key, const std::string& value) {
        // In production: span->SetAttribute(key, value);
        attributes_[key] = value;
    }
    
    std::string trace_id() const { 
        return trace_id_; 
    }
    
    TraceConfig config() const { return config_; }
    
private:
    void generate_trace_id() {
        // Generate valid trace ID (32 hex chars)
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<> dis(0, 15);
        std::ostringstream oss;
        for (int i = 0; i < 32; ++i) {
            oss << std::hex << dis(gen);
        }
        trace_id_ = oss.str();
    }
    
    TraceConfig config_;
    std::string span_name_;
    std::unordered_map<std::string, std::string> attributes_;
    bool span_active_;
    std::string trace_id_;
    std::chrono::steady_clock::time_point span_start_;
};

Tracer::Tracer(const TraceConfig& config) 
    : impl_(std::make_unique<Impl>(config)) {}

Tracer::~Tracer() {
    if (impl_) {
        impl_->end_span();  // Ensure span is ended
    }
}

void Tracer::start_span(const std::string& name) { 
    impl_->start_span(name); 
}

void Tracer::end_span() { 
    impl_->end_span(); 
}

void Tracer::add_attribute(const std::string& key, const std::string& value) {
    impl_->add_attribute(key, value);
}

std::string Tracer::trace_id() const { 
    return impl_->trace_id(); 
}

} // namespace cppload::otel
