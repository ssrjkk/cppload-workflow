#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include "cppload/net/http_client.hpp"
#include "cppload/metrics/collector.hpp"

namespace py = pybind11;

PYBIND11_MODULE(_cppload, m) {
    m.doc() = "cppload-pro: Enterprise Load Testing Platform (C++ core)";
    
    py::class_<cppload::net::HttpRequest>(m, "HttpRequest")
        .def(py::init<>())
        .def_readwrite("method", &cppload::net::HttpRequest::method)
        .def_readwrite("target", &cppload::net::HttpRequest::target)
        .def_readwrite("body", &cppload::net::HttpRequest::body)
        .def_readwrite("headers", &cppload::net::HttpRequest::headers)
        .def_readwrite("host", &cppload::net::HttpRequest::host)
        .def_readwrite("port", &cppload::net::HttpRequest::port);
    
    py::class_<cppload::net::HttpResponse>(m, "HttpResponse")
        .def(py::init<>())
        .def_readwrite("status_code", &cppload::net::HttpResponse::status_code)
        .def_readwrite("body", &cppload::net::HttpResponse::body)
        .def_readwrite("headers", &cppload::net::HttpResponse::headers)
        .def_property("latency_us", 
            [](const cppload::net::HttpResponse& r) { return r.latency.count(); });
    
    py::class_<cppload::net::HttpClient>(m, "HttpClient")
        .def(py::init<boost::asio::io_context&>())
        .def("async_request", [](cppload::net::HttpClient& client, 
                                 const cppload::net::HttpRequest& req) {
            py::gil_scoped_release release;
            // Placeholder for async callback to Python
        })
        .def("set_timeout", &cppload::net::HttpClient::set_timeout)
        .def("set_keep_alive", &cppload::net::HttpClient::set_keep_alive);
    
    py::class_<cppload::metrics::MetricsCollector>(m, "MetricsCollector")
        .def(py::init<>())
        .def("record_request", [](cppload::metrics::MetricsCollector& m, 
                                  uint16_t status, uint64_t latency_us,
                                  size_t sent, size_t received) {
            m.record_request(status, std::chrono::microseconds(latency_us), sent, received);
        })
        .def("requests_per_second", &cppload::metrics::MetricsCollector::requests_per_second)
        .def("error_rate", &cppload::metrics::MetricsCollector::error_rate)
        .def("reset", &cppload::metrics::MetricsCollector::reset);
}
