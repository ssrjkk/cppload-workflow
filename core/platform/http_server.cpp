// @author ssrjkk | volley
#include "cppload/platform/http_server.hpp"

#include <nlohmann/json.hpp>

#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/version.hpp>

#include <iostream>
#include <utility>

namespace beast = boost::beast;
namespace http = beast::http;
namespace asio = boost::asio;
using tcp = asio::ip::tcp;

namespace cppload::platform {

using json = nlohmann::json;

void to_json(json& j, const Project& p) {
    j = json{
        {"id", p.id}, {"name", p.name}, {"description", p.description},
        {"created_at", std::to_string(
            std::chrono::system_clock::to_time_t(p.created_at))}
    };
}

void to_json(json& j, const Scenario& s) {
    j = json{
        {"id", s.id}, {"project_id", s.project_id}, {"name", s.name},
        {"version", s.version}, {"yaml", s.yaml_content},
        {"created_at", std::to_string(
            std::chrono::system_clock::to_time_t(s.created_at))}
    };
}

void to_json(json& j, const RunResult& r) {
    j = json{
        {"total_requests", r.total_requests},
        {"successful_requests", r.successful_requests},
        {"failed_requests", r.failed_requests},
        {"error_rate_pct", r.error_rate_pct},
        {"throughput_rps", r.throughput_rps},
        {"p50_latency_ms", r.p50_latency_ms},
        {"p95_latency_ms", r.p95_latency_ms},
        {"p99_latency_ms", r.p99_latency_ms}
    };
}

void to_json(json& j, const Run& r) {
    j = json{
        {"id", r.id}, {"project_id", r.project_id},
        {"scenario_id", r.scenario_id}, {"environment", r.environment},
        {"status", to_string(r.status)},
        {"started_at", std::to_string(
            std::chrono::system_clock::to_time_t(r.started_at))}
    };
    if (r.finished_at.has_value()) {
        j["finished_at"] = std::to_string(
            std::chrono::system_clock::to_time_t(r.finished_at.value()));
    }
    if (r.result.has_value()) {
        j["result"] = r.result.value();
    }
}

void to_json(json& j, const PolicyRule& r) {
    j = json{{"metric", r.metric}, {"op", r.op}, {"threshold", r.threshold}};
}

void to_json(json& j, const Policy& p) {
    j = json{
        {"id", p.id}, {"project_id", p.project_id}, {"name", p.name},
        {"rules", p.rules},
        {"created_at", std::to_string(
            std::chrono::system_clock::to_time_t(p.created_at))}
    };
}

void to_json(json& j, const PolicyViolation& v) {
    j = json{
        {"metric", v.rule_metric}, {"message", v.message},
        {"severity", to_string(v.severity)}
    };
}

void to_json(json& j, const PolicyEvaluation& e) {
    j = json{
        {"run_id", e.run_id}, {"policy_id", e.policy_id},
        {"passed", e.passed}, {"violations", e.violations}
    };
}

namespace {

auto from_json_policy_rule(const json& j) -> PolicyRule {
    return PolicyRule{
        j.at("metric").get<std::string>(),
        j.at("op").get<std::string>(),
        j.at("threshold").get<double>()
    };
}

auto extract_path(const std::string& target) -> std::string {
    auto pos = target.find('?');
    if (pos == std::string::npos) {
        pos = target.find('#');
    }
    return (pos == std::string::npos) ? target : target.substr(0, pos);
}

auto error_response(http::status status, const std::string& message) -> http::response<http::string_body> {
    auto body = json{{"error", message}}.dump();
    auto res = http::response<http::string_body>{status, http::version()};
    res.set(http::field::content_type, "application/json");
    res.body() = std::move(body);
    res.prepare_payload();
    return res;
}

auto ok_response(const json& body) -> http::response<http::string_body> {
    auto res = http::response<http::string_body>{http::status::ok, http::version()};
    res.set(http::field::content_type, "application/json");
    res.body() = body.dump();
    res.prepare_payload();
    return res;
}

auto created_response(const json& body) -> http::response<http::string_body> {
    auto res = http::response<http::string_body>{http::status::created, http::version()};
    res.set(http::field::content_type, "application/json");
    res.body() = body.dump();
    res.prepare_payload();
    return res;
}

auto method_not_allowed_response() -> http::response<http::string_body> {
    return error_response(http::status::method_not_allowed, "method not allowed");
}

auto not_found_response() -> http::response<http::string_body> {
    return error_response(http::status::not_found, "not found");
}

auto status_to_http(PlatformError e) -> http::status {
    switch (e) {
        case PlatformError::not_found:     return http::status::not_found;
        case PlatformError::already_exists: return http::status::conflict;
        case PlatformError::invalid_input:  return http::status::bad_request;
        case PlatformError::invalid_state:  return http::status::conflict;
    }
    return http::status::internal_server_error;
}

template <typename T>
auto result_to_response(Result<T, PlatformError> res) -> http::response<http::string_body> {
    if (!res.has_value()) {
        return error_response(status_to_http(res.error()), "error");
    }
    json j = res.value();
    return ok_response(j);
}

} // namespace

HttpServer::HttpServer(std::shared_ptr<Repository> repo,
                       std::shared_ptr<PolicyEvaluator> evaluator,
                       std::string address, uint16_t port)
    : repo_(std::move(repo))
    , evaluator_(std::move(evaluator))
    , address_(std::move(address))
    , port_(port)
    , acceptor_{ioc_, tcp::endpoint{asio::ip::make_address(address_), port_}}
{
}

HttpServer::~HttpServer() {
    stop();
}

void HttpServer::start() {
    running_ = true;
    accept_thread_ = std::thread([this] { accept_loop(); });
}

void HttpServer::stop() {
    running_ = false;
    boost::system::error_code ec;
    acceptor_.close(ec);
    if (accept_thread_.joinable()) {
        accept_thread_.join();
    }
}

void HttpServer::accept_loop() {
    while (running_) {
        auto ec = beast::error_code{};
        auto socket = acceptor_.accept(ec);
        if (ec) {
            if (running_) {
                std::cerr << "accept error: " << ec.message() << "\n";
            }
            continue;
        }
        handle_session(std::move(socket));
    }
}

void HttpServer::handle_session(boost::asio::ip::tcp::socket socket) {
    auto buffer = beast::flat_buffer{};
    auto req = http::request<http::string_body>{};
    auto ec = beast::error_code{};

    http::read(socket, buffer, req, ec);
    if (ec) {
        return;
    }

    auto path = extract_path(std::string(req.target()));
    auto res = route(req.method(), path, std::string(req.body()));

    res.keep_alive(req.keep_alive());
    res.set(http::field::server, "volley-control-plane");
    http::write(socket, res, ec);

    socket.shutdown(asio::socket_base::shutdown_send, ec);
}

auto HttpServer::route(http::verb method, const std::string& path,
                       const std::string& body) -> http::response<http::string_body> {
    if (path == "/api/v1/projects") {
        return handle_projects(method, body);
    }
    if (path.rfind("/api/v1/projects/", 0) == 0) {
        auto rest = path.substr(17);
        auto slash = rest.find('/');
        if (slash == std::string::npos) {
            return handle_project(method, rest, body);
        }
        auto project_id = rest.substr(0, slash);
        auto sub = rest.substr(slash);
        if (sub == "/scenarios") {
            return handle_scenarios(method, project_id, body);
        }
        if (sub == "/runs") {
            return handle_runs(method, project_id, body);
        }
        if (sub == "/policies") {
            return handle_policies(method, project_id, body);
        }
    }
    if (path == "/api/v1/evaluate") {
        return handle_evaluate(method, body);
    }
    if (path == "/health") {
        return ok_response(json{{"status", "ok"}});
    }
    return not_found_response();
}

auto HttpServer::handle_projects(http::verb method,
                                 const std::string& body) -> http::response<http::string_body> {
    if (method == http::verb::get) {
        return ok_response(json(repo_->list_projects()));
    }
    if (method == http::verb::post) {
        auto j = json::parse(body, nullptr, false);
        if (j.is_discarded()) {
            return error_response(http::status::bad_request, "invalid JSON");
        }
        auto name = j.value("name", "");
        auto desc = j.value("description", "");
        auto res = repo_->create_project(std::move(name), std::move(desc));
        if (!res.has_value()) {
            return error_response(status_to_http(res.error()), "invalid input");
        }
        return created_response(json(res.value()));
    }
    return method_not_allowed_response();
}

auto HttpServer::handle_project(http::verb method, const std::string& id,
                                const std::string& /*body*/) -> http::response<http::string_body> {
    if (method == http::verb::get) {
        return result_to_response(repo_->get_project(id));
    }
    if (method == http::verb::delete_) {
        auto res = repo_->delete_project(id);
        if (!res.has_value()) {
            return error_response(status_to_http(res.error()), "not found");
        }
        return ok_response(json{{"deleted", true}});
    }
    return method_not_allowed_response();
}

auto HttpServer::handle_scenarios(http::verb method, const std::string& project_id,
                                  const std::string& body) -> http::response<http::string_body> {
    if (method == http::verb::get) {
        return ok_response(json(repo_->list_scenarios(project_id)));
    }
    if (method == http::verb::post) {
        auto j = json::parse(body, nullptr, false);
        if (j.is_discarded()) {
            return error_response(http::status::bad_request, "invalid JSON");
        }
        auto res = repo_->create_scenario(
            project_id,
            j.value("name", ""),
            j.value("version", ""),
            j.value("yaml", ""));
        if (!res.has_value()) {
            return error_response(status_to_http(res.error()), "error");
        }
        return created_response(json(res.value()));
    }
    return method_not_allowed_response();
}

auto HttpServer::handle_runs(http::verb method, const std::string& project_id,
                             const std::string& body) -> http::response<http::string_body> {
    if (method == http::verb::get) {
        return ok_response(json(repo_->list_runs(project_id)));
    }
    if (method == http::verb::post) {
        auto j = json::parse(body, nullptr, false);
        if (j.is_discarded()) {
            return error_response(http::status::bad_request, "invalid JSON");
        }
        auto res = repo_->create_run(
            project_id,
            j.value("scenario_id", ""),
            j.value("environment", ""));
        if (!res.has_value()) {
            return error_response(status_to_http(res.error()), "error");
        }
        return created_response(json(res.value()));
    }
    return method_not_allowed_response();
}

auto HttpServer::handle_policies(http::verb method, const std::string& project_id,
                                 const std::string& body) -> http::response<http::string_body> {
    if (method == http::verb::get) {
        return ok_response(json(repo_->list_policies(project_id)));
    }
    if (method == http::verb::post) {
        auto j = json::parse(body, nullptr, false);
        if (j.is_discarded()) {
            return error_response(http::status::bad_request, "invalid JSON");
        }
        auto rules = std::vector<PolicyRule>{};
        if (j.contains("rules") && j["rules"].is_array()) {
            for (const auto& rj : j["rules"]) {
                rules.push_back(from_json_policy_rule(rj));
            }
        }
        auto res = repo_->create_policy(
            project_id, j.value("name", ""), std::move(rules));
        if (!res.has_value()) {
            return error_response(status_to_http(res.error()), "error");
        }
        return created_response(json(res.value()));
    }
    return method_not_allowed_response();
}

auto HttpServer::handle_evaluate(http::verb method,
                                 const std::string& body) -> http::response<http::string_body> {
    if (method != http::verb::post) {
        return method_not_allowed_response();
    }
    auto j = json::parse(body, nullptr, false);
    if (j.is_discarded()) {
        return error_response(http::status::bad_request, "invalid JSON");
    }
    auto run_id = j.value("run_id", "");
    auto policy_id = j.value("policy_id", "");
    if (run_id.empty() || policy_id.empty()) {
        return error_response(http::status::bad_request, "run_id and policy_id required");
    }
    auto evaluation = evaluator_->evaluate(run_id, policy_id);
    return ok_response(json(evaluation));
}

} // namespace cppload::platform
