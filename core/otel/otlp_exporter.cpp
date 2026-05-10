#include "cppload/otel/exporter.hpp"
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/connect.hpp>
#include <nlohmann/json.hpp>
#include <string>
#include <sstream>
#include <chrono>
#include <vector>
#include <random>
#include <mutex>

namespace beast = boost::beast;
namespace http = beast::http;
namespace asio = boost::asio;
using json = nlohmann::json;

namespace cppload::otel {

namespace {

struct SpanData {
    std::string name;
    std::string trace_id;
    std::string span_id;
    std::string parent_span_id;
    std::chrono::system_clock::time_point start_time;
    std::chrono::system_clock::time_point end_time;
    std::unordered_map<std::string, std::string> attributes;
    bool ended{false};
};

std::string random_hex(size_t len) {
    static thread_local std::mt19937 gen(std::random_device{}());
    static thread_local std::uniform_int_distribution<> dis(0, 15);
    std::ostringstream oss;
    for (size_t i = 0; i < len; ++i) oss << std::hex << dis(gen);
    return oss.str();
}

int64_t to_nanos(std::chrono::system_clock::time_point tp) {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
        tp.time_since_epoch()).count();
}

void do_post_json(
    const std::string& host,
    const std::string& port,
    const std::string& target,
    const json& body,
    std::chrono::seconds timeout = std::chrono::seconds(5))
{
    beast::error_code ec;
    asio::io_context ioc;
    asio::ip::tcp::resolver resolver(ioc);
    beast::tcp_stream stream(ioc);

    auto results = resolver.resolve(host, port, ec);
    if (ec) return;

    stream.expires_after(timeout);
    stream.connect(results, ec);
    if (ec) return;

    std::string body_str = body.dump();
    http::request<http::string_body> req{http::verb::post, target, 11};
    req.set(http::field::host, host);
    req.set(http::field::user_agent, "cppload-pro/1.0");
    req.set(http::field::content_type, "application/json");
    req.body() = body_str;
    req.prepare_payload();

    stream.expires_after(timeout);
    http::write(stream, req, ec);
    if (ec) return;

    beast::flat_buffer buffer;
    http::response<http::string_body> res;
    stream.expires_after(timeout);
    http::read(stream, buffer, res, ec);
    if (ec && ec != http::error::end_of_stream) return;

    beast::error_code shutdown_ec;
    stream.socket().shutdown(asio::ip::tcp::socket::shutdown_both, shutdown_ec);
}

} // anonymous namespace

class Tracer::Impl {
public:
    explicit Impl(const TraceConfig& config)
        : config_(config), span_active_(false)
        , trace_id_(random_hex(32))
    {
        parse_endpoint();
    }

    ~Impl() {
        flush();
    }

    void start_span(const std::string& name) {
        if (span_active_) end_span();

        span_name_ = name;
        span_active_ = true;
        span_start_ = std::chrono::system_clock::now();
        current_span_id_ = random_hex(16);
        parent_span_id_ = last_span_id_;

        add_attribute("service.name", config_.service_name);
        add_attribute("service.version", config_.service_version);
    }

    void end_span() {
        if (!span_active_) return;

        SpanData span;
        span.name = span_name_;
        span.trace_id = trace_id_;
        span.span_id = current_span_id_;
        span.parent_span_id = parent_span_id_;
        span.start_time = span_start_;
        span.end_time = std::chrono::system_clock::now();
        span.attributes = attributes_;
        span.ended = true;

        last_span_id_ = current_span_id_;

        {
            std::lock_guard<std::mutex> lock(spans_mutex_);
            completed_spans_.push_back(std::move(span));
        }

        attributes_.clear();
        span_active_ = false;

        maybe_export();
    }

    void add_attribute(const std::string& key, const std::string& value) {
        attributes_[key] = value;
    }

    std::string trace_id() const { return trace_id_; }

private:
    void parse_endpoint() {
        auto proto_end = config_.endpoint.find("://");
        auto start = (proto_end != std::string::npos) ? proto_end + 3 : 0;
        auto path_start = config_.endpoint.find("/", start);
        endpoint_target_ = (path_start != std::string::npos)
            ? config_.endpoint.substr(path_start) : "/";
        auto host_port = (path_start != std::string::npos)
            ? config_.endpoint.substr(start, path_start - start)
            : config_.endpoint.substr(start);
        auto colon = host_port.find(":");
        if (colon != std::string::npos) {
            endpoint_host_ = host_port.substr(0, colon);
            endpoint_port_ = host_port.substr(colon + 1);
        } else {
            endpoint_host_ = host_port;
            endpoint_port_ = "4318";
        }

        if (endpoint_target_ == "/" || endpoint_target_.empty()) {
            endpoint_target_ = "/v1/traces";
        }
    }

    void do_export(const std::vector<SpanData>& spans_to_export) {
        if (spans_to_export.empty()) return;

        json resource_spans;
        resource_spans["resource"]["attributes"] = json::array({
            {{"key", "service.name"}, {"value", {"stringValue", config_.service_name}}},
            {{"key", "service.version"}, {"value", {"stringValue", config_.service_version}}}
        });

        json scope_spans;
        scope_spans["scope"]["name"] = "cppload-pro";
        scope_spans["scope"]["version"] = "1.0.0";

        for (const auto& span : spans_to_export) {
            json s;
            s["traceId"] = span.trace_id;
            s["spanId"] = span.span_id;
            if (!span.parent_span_id.empty()) {
                s["parentSpanId"] = span.parent_span_id;
            }
            s["name"] = span.name;
            s["kind"] = 2; // SPAN_KIND_CLIENT
            s["startTimeUnixNano"] = std::to_string(to_nanos(span.start_time));
            s["endTimeUnixNano"] = std::to_string(to_nanos(span.end_time));

            json attrs = json::array();
            for (const auto& [k, v] : span.attributes) {
                attrs.push_back({
                    {"key", k},
                    {"value", {"stringValue", v}}
                });
            }
            if (!attrs.empty()) s["attributes"] = attrs;

            s["status"]["code"] = 1; // STATUS_CODE_OK

            scope_spans["spans"].push_back(std::move(s));
        }

        resource_spans["scopeSpans"] = json::array({scope_spans});

        json payload;
        payload["resourceSpans"] = json::array({resource_spans});

        do_post_json(endpoint_host_, endpoint_port_,
            endpoint_target_, payload);
    }

    void flush() {
        std::vector<SpanData> remaining;
        {
            std::lock_guard<std::mutex> lock(spans_mutex_);
            remaining.swap(completed_spans_);
        }
        do_export(remaining);
    }

    void maybe_export() {
        if (config_.sample_rate < 1.0) {
            static thread_local std::mt19937 rng(std::random_device{}());
            std::uniform_real_distribution<> dist(0.0, 1.0);
            if (dist(rng) > config_.sample_rate) {
                std::lock_guard<std::mutex> lock(spans_mutex_);
                completed_spans_.clear();
                return;
            }
        }

        std::vector<SpanData> spans_to_export;
        {
            std::lock_guard<std::mutex> lock(spans_mutex_);
            if (completed_spans_.size() >= 64) {
                spans_to_export.swap(completed_spans_);
            }
        }
        do_export(spans_to_export);
    }

    TraceConfig config_;
    std::string span_name_;
    std::unordered_map<std::string, std::string> attributes_;
    bool span_active_;
    std::string trace_id_;
    std::string current_span_id_;
    std::string parent_span_id_;
    std::string last_span_id_;
    std::chrono::system_clock::time_point span_start_;

    std::vector<SpanData> completed_spans_;
    std::mutex spans_mutex_;

    std::string endpoint_host_;
    std::string endpoint_port_;
    std::string endpoint_target_;
};

Tracer::Tracer(const TraceConfig& config)
    : impl_(std::make_unique<Impl>(config)) {}

Tracer::~Tracer() = default;

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
