#pragma once

#include <string>
#include <unordered_map>
#include <memory>

namespace cppload::otel {

struct TraceConfig {
    std::string endpoint{"http://localhost:4317"};
    double sample_rate{1.0};
    std::string service_name{"cppload-pro"};
    std::string service_version{"1.0.0"};
};

class Tracer {
public:
    explicit Tracer(const TraceConfig& config = {});
    ~Tracer() noexcept;
    
    Tracer(const Tracer&) = delete;
    Tracer& operator=(const Tracer&) = delete;
    
    void start_span(const std::string& name);
    void end_span();
    void add_attribute(const std::string& key, const std::string& value);
    
    std::string trace_id() const;
    
private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace cppload::otel
