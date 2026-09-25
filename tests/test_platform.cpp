// @author ssrjkk | volley
#include "cppload/platform/http_server.hpp"
#include "cppload/platform/repository.hpp"

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/asio/connect.hpp>
#include <boost/asio/ip/tcp.hpp>

#include <chrono>
#include <cstdint>
#include <thread>

namespace beast = boost::beast;
namespace http = beast::http;
namespace asio = boost::asio;
using tcp = asio::ip::tcp;
using json = nlohmann::json;

namespace {

auto make_request(http::verb method, const std::string& target,
                  const std::string& body = "", uint16_t port = 19876)
    -> http::response<http::string_body> {
    auto ioc = boost::asio::io_context{};
    auto stream = tcp::iostream{asio::ip::make_address("127.0.0.1"), port};
    auto req = http::request<http::string_body>{method, target, 11};
    req.set(http::field::host, "localhost");
    if (!body.empty()) {
        req.body() = body;
        req.set(http::field::content_type, "application/json");
        req.prepare_payload();
    }
    http::write(stream, req);
    auto buffer = beast::flat_buffer{};
    auto res = http::response<http::string_body>{};
    http::read(stream, buffer, res);
    return res;
}

} // namespace

// --- Repository tests ---

TEST(PlatformRepository, CreateAndGetProject) {
    auto repo = cppload::platform::make_in_memory_repository();
    auto res = repo->create_project("test-project", "A test project");
    ASSERT_TRUE(res.has_value());
    EXPECT_EQ(res.value().name, "test-project");
    EXPECT_FALSE(res.value().id.empty());

    auto get_res = repo->get_project(res.value().id);
    ASSERT_TRUE(get_res.has_value());
    EXPECT_EQ(get_res.value().name, "test-project");
}

TEST(PlatformRepository, ListProjects) {
    auto repo = cppload::platform::make_in_memory_repository();
    repo->create_project("proj-a", "");
    repo->create_project("proj-b", "");
    auto list = repo->list_projects();
    EXPECT_EQ(list.size(), 2U);
}

TEST(PlatformRepository, DeleteProject) {
    auto repo = cppload::platform::make_in_memory_repository();
    auto res = repo->create_project("to-delete", "");
    ASSERT_TRUE(res.has_value());
    auto id = res.value().id;

    auto del = repo->delete_project(id);
    ASSERT_TRUE(del.has_value());

    auto get = repo->get_project(id);
    EXPECT_FALSE(get.has_value());
    EXPECT_EQ(get.error(), cppload::platform::PlatformError::not_found);
}

TEST(PlatformRepository, GetProjectNotFound) {
    auto repo = cppload::platform::make_in_memory_repository();
    auto res = repo->get_project("nonexistent");
    ASSERT_FALSE(res.has_value());
    EXPECT_EQ(res.error(), cppload::platform::PlatformError::not_found);
}

TEST(PlatformRepository, CreateRunLifecycle) {
    auto repo = cppload::platform::make_in_memory_repository();
    auto proj = repo->create_project("proj", "");
    ASSERT_TRUE(proj.has_value());

    auto run = repo->create_run(proj.value().id, "scenario-1", "staging");
    ASSERT_TRUE(run.has_value());
    EXPECT_EQ(run.value().status, cppload::platform::RunStatus::pending);

    auto result = cppload::platform::RunResult{
        1000, 990, 10, 1.0, 500.0, 5.0, 15.0, 50.0};
    auto finish = repo->finish_run(run.value().id,
                                   cppload::platform::RunStatus::completed,
                                   result);
    ASSERT_TRUE(finish.has_value());

    auto updated = repo->get_run(run.value().id);
    ASSERT_TRUE(updated.has_value());
    EXPECT_EQ(updated.value().status, cppload::platform::RunStatus::completed);
    ASSERT_TRUE(updated.value().result.has_value());
    EXPECT_EQ(updated.value().result->total_requests, 1000);
}

TEST(PlatformRepository, FinishRunNotFound) {
    auto repo = cppload::platform::make_in_memory_repository();
    auto result = cppload::platform::RunResult{};
    auto res = repo->finish_run("nonexistent",
                                cppload::platform::RunStatus::completed,
                                result);
    ASSERT_FALSE(res.has_value());
}

TEST(PlatformRepository, CreateScenarioRequiresProject) {
    auto repo = cppload::platform::make_in_memory_repository();
    auto res = repo->create_scenario("nonexistent", "name", "1.0", "yaml");
    ASSERT_FALSE(res.has_value());
    EXPECT_EQ(res.error(), cppload::platform::PlatformError::not_found);
}

TEST(PlatformRepository, CreatePolicyRequiresRules) {
    auto repo = cppload::platform::make_in_memory_repository();
    auto proj = repo->create_project("proj", "");
    ASSERT_TRUE(proj.has_value());
    auto res = repo->create_policy(proj.value().id, "empty-policy", {});
    ASSERT_FALSE(res.has_value());
    EXPECT_EQ(res.error(), cppload::platform::PlatformError::invalid_input);
}

// --- Policy evaluator tests ---

TEST(PlatformPolicy, EvaluatePassingPolicy) {
    auto repo = cppload::platform::make_in_memory_repository();
    auto proj = repo->create_project("proj", "");
    auto run = repo->create_run(proj.value().id, "sc", "env");
    auto result = cppload::platform::RunResult{
        1000, 990, 10, 1.0, 500.0, 5.0, 15.0, 50.0};
    repo->finish_run(run.value().id, cppload::platform::RunStatus::completed, result);

    auto rules = std::vector<cppload::platform::PolicyRule>{
        {"error_rate_pct", "<", 5.0},
        {"p99_latency_ms", "<", 100.0}
    };
    auto policy = repo->create_policy(proj.value().id, "sla", std::move(rules));

    auto evaluator = cppload::platform::PolicyEvaluator(repo);
    auto evaluation = evaluator.evaluate(run.value().id, policy.value().id);
    EXPECT_TRUE(evaluation.passed);
    EXPECT_TRUE(evaluation.violations.empty());
}

TEST(PlatformPolicy, EvaluateFailingPolicy) {
    auto repo = cppload::platform::make_in_memory_repository();
    auto proj = repo->create_project("proj", "");
    auto run = repo->create_run(proj.value().id, "sc", "env");
    auto result = cppload::platform::RunResult{
        1000, 800, 200, 20.0, 500.0, 5.0, 15.0, 200.0};
    repo->finish_run(run.value().id, cppload::platform::RunStatus::completed, result);

    auto rules = std::vector<cppload::platform::PolicyRule>{
        {"error_rate_pct", "<", 5.0},
        {"p99_latency_ms", "<", 100.0}
    };
    auto policy = repo->create_policy(proj.value().id, "strict-sla", std::move(rules));

    auto evaluator = cppload::platform::PolicyEvaluator(repo);
    auto evaluation = evaluator.evaluate(run.value().id, policy.value().id);
    EXPECT_FALSE(evaluation.passed);
    EXPECT_EQ(evaluation.violations.size(), 2U);
}

TEST(PlatformPolicy, EvaluateRunWithoutResult) {
    auto repo = cppload::platform::make_in_memory_repository();
    auto proj = repo->create_project("proj", "");
    auto run = repo->create_run(proj.value().id, "sc", "env");
    auto rules = std::vector<cppload::platform::PolicyRule>{
        {"error_rate_pct", "<", 5.0}
    };
    auto policy = repo->create_policy(proj.value().id, "sla", std::move(rules));

    auto evaluator = cppload::platform::PolicyEvaluator(repo);
    auto evaluation = evaluator.evaluate(run.value().id, policy.value().id);
    EXPECT_FALSE(evaluation.passed);
}

// --- HTTP server tests ---

TEST(PlatformHttpServer, HealthEndpoint) {
    auto repo = cppload::platform::make_in_memory_repository();
    auto evaluator = std::make_shared<cppload::platform::PolicyEvaluator>(repo);
    auto server = cppload::platform::HttpServer(repo, evaluator, "127.0.0.1", 19876);
    server.start();
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    auto res = make_request(http::verb::get, "/health");
    EXPECT_EQ(res.result(), http::status::ok);
    auto j = json::parse(res.body());
    EXPECT_EQ(j["status"], "ok");

    server.stop();
}

TEST(PlatformHttpServer, CreateAndListProjects) {
    auto repo = cppload::platform::make_in_memory_repository();
    auto evaluator = std::make_shared<cppload::platform::PolicyEvaluator>(repo);
    auto server = cppload::platform::HttpServer(repo, evaluator, "127.0.0.1", 19877);
    server.start();
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    auto create_res = make_request(http::verb::post, "/api/v1/projects",
                                   R"({"name":"my-project","description":"test"})", 19877);
    EXPECT_EQ(create_res.result(), http::status::created);
    auto created = json::parse(create_res.body());
    EXPECT_EQ(created["name"], "my-project");

    auto list_res = make_request(http::verb::get, "/api/v1/projects", "", 19877);
    EXPECT_EQ(list_res.result(), http::status::ok);
    auto list = json::parse(list_res.body());
    EXPECT_EQ(list.size(), 1U);

    server.stop();
}

TEST(PlatformHttpServer, DeleteProject) {
    auto repo = cppload::platform::make_in_memory_repository();
    auto evaluator = std::make_shared<cppload::platform::PolicyEvaluator>(repo);
    auto server = cppload::platform::HttpServer(repo, evaluator, "127.0.0.1", 19878);
    server.start();
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    auto create_res = make_request(http::verb::post, "/api/v1/projects",
                                   R"({"name":"to-delete"})", 19878);
    auto id = json::parse(create_res.body())["id"].get<std::string>();

    auto del_res = make_request(http::verb::delete_,
                                "/api/v1/projects/" + id, "", 19878);
    EXPECT_EQ(del_res.result(), http::status::ok);

    auto get_res = make_request(http::verb::get,
                                "/api/v1/projects/" + id, "", 19878);
    EXPECT_EQ(get_res.result(), http::status::not_found);

    server.stop();
}

TEST(PlatformHttpServer, NotFoundRoute) {
    auto repo = cppload::platform::make_in_memory_repository();
    auto evaluator = std::make_shared<cppload::platform::PolicyEvaluator>(repo);
    auto server = cppload::platform::HttpServer(repo, evaluator, "127.0.0.1", 19879);
    server.start();
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    auto res = make_request(http::verb::get, "/nonexistent", "", 19879);
    EXPECT_EQ(res.result(), http::status::not_found);

    server.stop();
}
