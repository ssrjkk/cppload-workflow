#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <pybind11/chrono.h>
#include <pybind11/functional.h>
#include "cppload/net/http_client.hpp"
#include "cppload/net/connection_pool.hpp"
#include "cppload/metrics/collector.hpp"
#include "cppload/metrics/prometheus_exporter.hpp"
#include "cppload/core/token_bucket.hpp"
#include "cppload/security/auth_provider.hpp"
#include "cppload/security/tls_context.hpp"
#include "cppload/vault/vault_client.hpp"
#include "cppload/otel/exporter.hpp"
#include "cppload/scenario/engine.hpp"
#include <boost/asio/io_context.hpp>

namespace py = pybind11;
namespace net = cppload::net;
namespace metrics = cppload::metrics;
namespace security = cppload::security;
namespace vault = cppload::vault;
namespace otel = cppload::otel;
namespace scenario = cppload::scenario;

PYBIND11_MODULE(_cppload, m) {
    m.doc() = "cppload-pro: Enterprise Load Testing Platform (C++ core)";

    // --- HTTP Client ---
    py::class_<net::HttpRequest>(m, "HttpRequest")
        .def(py::init<>())
        .def_readwrite("method", &net::HttpRequest::method)
        .def_readwrite("target", &net::HttpRequest::target)
        .def_readwrite("body", &net::HttpRequest::body)
        .def_readwrite("headers", &net::HttpRequest::headers)
        .def_readwrite("host", &net::HttpRequest::host)
        .def_readwrite("port", &net::HttpRequest::port);

    py::class_<net::HttpResponse>(m, "HttpResponse")
        .def(py::init<>())
        .def_readwrite("status_code", &net::HttpResponse::status_code)
        .def_readwrite("body", &net::HttpResponse::body)
        .def_readwrite("headers", &net::HttpResponse::headers)
        .def_property("latency_us",
            [](const net::HttpResponse& r) { return r.latency.count(); });

    py::class_<net::HttpClient>(m, "HttpClient")
        .def(py::init<boost::asio::io_context&>())
        .def("set_timeout", &net::HttpClient::set_timeout)
        .def("set_keep_alive", &net::HttpClient::set_keep_alive);

    // --- Connection Pool ---
    py::class_<net::PoolConfig>(m, "PoolConfig")
        .def(py::init<>())
        .def_readwrite("min_connections", &net::PoolConfig::min_connections)
        .def_readwrite("max_connections", &net::PoolConfig::max_connections)
        .def_readwrite("idle_timeout", &net::PoolConfig::idle_timeout)
        .def_readwrite("keep_alive", &net::PoolConfig::keep_alive);

    py::class_<net::ConnectionPool>(m, "ConnectionPool")
        .def(py::init<boost::asio::io_context&, const net::PoolConfig&>(),
             py::arg("ioc"), py::arg("config") = net::PoolConfig{});

    // --- Metrics ---
    py::class_<metrics::MetricsCollector>(m, "MetricsCollector")
        .def(py::init<>())
        .def("record_request", [](metrics::MetricsCollector& m,
                                   uint16_t status, uint64_t latency_us,
                                   size_t sent, size_t received) {
            m.record_request(status, std::chrono::microseconds(latency_us), sent, received);
        })
        .def("requests_per_second", &metrics::MetricsCollector::requests_per_second)
        .def("error_rate", &metrics::MetricsCollector::error_rate)
        .def("p95_latency_us", &metrics::MetricsCollector::p95_latency_us)
        .def("p99_latency_us", &metrics::MetricsCollector::p99_latency_us)
        .def("reset", &metrics::MetricsCollector::reset)
        .def("snapshot", &metrics::MetricsCollector::snapshot);

    py::class_<metrics::RequestMetrics>(m, "RequestMetrics")
        .def(py::init<>())
        .def_readonly("total_requests", &metrics::RequestMetrics::total_requests)
        .def_readonly("successful_requests", &metrics::RequestMetrics::successful_requests)
        .def_readonly("failed_requests", &metrics::RequestMetrics::failed_requests)
        .def_readonly("total_bytes_sent", &metrics::RequestMetrics::total_bytes_sent)
        .def_readonly("total_bytes_received", &metrics::RequestMetrics::total_bytes_received);

    // --- TokenBucket ---
    py::class_<cppload::TokenBucket>(m, "TokenBucket")
        .def(py::init<double, double>(), py::arg("rate"), py::arg("burst") = 0.0)
        .def("consume", &cppload::TokenBucket::consume)
        .def("try_consume", &cppload::TokenBucket::try_consume)
        .def("set_rate", &cppload::TokenBucket::set_rate)
        .def("set_burst", &cppload::TokenBucket::set_burst);

    // --- Auth ---
    py::enum_<security::AuthType>(m, "AuthType")
        .value("NONE", security::AuthType::NONE)
        .value("API_KEY", security::AuthType::API_KEY)
        .value("BEARER_TOKEN", security::AuthType::BEARER_TOKEN)
        .value("OAUTH2", security::AuthType::OAUTH2)
        .value("MTLS", security::AuthType::MTLS)
        .export_values();

    py::class_<security::AuthConfig>(m, "AuthConfig")
        .def(py::init<>())
        .def_readwrite("type", &security::AuthConfig::type)
        .def_readwrite("api_key", &security::AuthConfig::api_key)
        .def_readwrite("token", &security::AuthConfig::token)
        .def_readwrite("client_id", &security::AuthConfig::client_id)
        .def_readwrite("client_secret", &security::AuthConfig::client_secret)
        .def_readwrite("token_endpoint", &security::AuthConfig::token_endpoint);

    py::class_<security::AuthProvider>(m, "AuthProvider")
        .def(py::init<const security::AuthConfig&>(),
             py::arg("config") = security::AuthConfig{})
        .def("apply_headers", &security::AuthProvider::apply_headers)
        .def("get_auth_header", &security::AuthProvider::get_auth_header)
        .def("refresh_token", &security::AuthProvider::refresh_token)
        .def("is_expired", &security::AuthProvider::is_expired);

    // --- TLS ---
    py::class_<security::TlsConfig>(m, "TlsConfig")
        .def(py::init<>())
        .def_readwrite("verify_peer", &security::TlsConfig::verify_peer)
        .def_readwrite("cert_chain_file", &security::TlsConfig::cert_chain_file)
        .def_readwrite("private_key_file", &security::TlsConfig::private_key_file)
        .def_readwrite("ca_cert_file", &security::TlsConfig::ca_cert_file)
        .def_readwrite("use_mtls", &security::TlsConfig::use_mtls);

    py::class_<security::TlsContext>(m, "TlsContext")
        .def(py::init<const security::TlsConfig&>(),
             py::arg("config") = security::TlsConfig{})
        .def("is_mtls_enabled", &security::TlsContext::is_mtls_enabled);

    // --- Vault ---
    py::class_<vault::VaultConfig>(m, "VaultConfig")
        .def(py::init<>())
        .def_readwrite("address", &vault::VaultConfig::address)
        .def_readwrite("token", &vault::VaultConfig::token)
        .def_readwrite("engine_path", &vault::VaultConfig::engine_path)
        .def_readwrite("timeout_seconds", &vault::VaultConfig::timeout_seconds);

    py::class_<vault::VaultClient>(m, "VaultClient")
        .def(py::init<const vault::VaultConfig&>(),
             py::arg("config") = vault::VaultConfig{})
        .def("is_connected", &vault::VaultClient::is_connected)
        .def("get_secret", &vault::VaultClient::get_secret,
             py::arg("path"), py::arg("key"))
        .def("get_secret_map", &vault::VaultClient::get_secret_map,
             py::arg("path"))
        .def("put_secret", &vault::VaultClient::put_secret,
             py::arg("path"), py::arg("data"))
        .def("get_database_creds", &vault::VaultClient::get_database_creds,
             py::arg("role_name"))
        .def("get_approle_token", &vault::VaultClient::get_approle_token,
             py::arg("role_id"), py::arg("secret_id"))
        .def("last_error", &vault::VaultClient::last_error);

    // --- OTLP Tracer ---
    py::class_<otel::TraceConfig>(m, "TraceConfig")
        .def(py::init<>())
        .def_readwrite("endpoint", &otel::TraceConfig::endpoint)
        .def_readwrite("sample_rate", &otel::TraceConfig::sample_rate)
        .def_readwrite("service_name", &otel::TraceConfig::service_name);

    py::class_<otel::Tracer>(m, "Tracer")
        .def(py::init<const otel::TraceConfig&>(),
             py::arg("config") = otel::TraceConfig{})
        .def("start_span", &otel::Tracer::start_span)
        .def("end_span", &otel::Tracer::end_span)
        .def("add_attribute", &otel::Tracer::add_attribute)
        .def("trace_id", &otel::Tracer::trace_id);

    // --- Scenario Engine ---
    py::class_<scenario::ScenarioConfig>(m, "ScenarioConfig")
        .def(py::init<>())
        .def_readwrite("test_id", &scenario::ScenarioConfig::test_id);
}
