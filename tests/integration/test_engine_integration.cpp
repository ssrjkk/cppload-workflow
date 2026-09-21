// @author ssrjkk | cppload
#include <gtest/gtest.h>
#include "cppload/scenario/engine.hpp"
#include "mock_server.hpp"
#include <atomic>
#include <filesystem>
#include <fstream>

// The engine must sustain the stage's target_rps for the full stage duration.
// The old one-pass engine issued exactly concurrent_users x steps = 10 requests
// per stage and then quit, so the floor proves the worker loop keeps issuing
// for the whole 2s. 200 rps x 2s = ~400 requests; the floor leaves headroom for
// slow machines, the cap guards against a runaway request loop.
TEST(ScenarioEngineIntegrationTest, SustainsLoadOverStageDuration) {
    MockHttpServer server;
    server.set_handler([](const auto&) {
        http::response<http::string_body> res;
        res.result(http::status::ok);
        res.set(http::field::content_type, "text/plain");
        res.body() = "OK";
        res.prepare_payload();
        return res;
    });
    ASSERT_TRUE(server.start());

    auto config_path =
        std::filesystem::temp_directory_path() / "cppload_engine_int.yaml";
    {
        std::ofstream f(config_path);
        f << "version: \"1.0\"\n"
          << "test_id: \"engine-int\"\n"
          << "target:\n"
          << "  base_url: http://127.0.0.1:" << server.port() << "\n"
          << "  protocol: http1.1\n"
          << "load_profile:\n"
          << "  - stage: sustain\n"
          << "    duration: 2s\n"
          << "    target_rps: 200\n"
          << "    concurrent_users: 10\n"
          << "scenarios:\n"
          << "  - name: default\n"
          << "    weight: 100\n"
          << "    steps:\n"
          << "      - http:\n"
          << "          method: GET\n"
          << "          path: /sustain\n"
          << "          assertions:\n"
          << "            - status_code == 200\n"
          << "sla:\n"
          << "  error_rate: \"< 10%\"\n"
          << "  p99_latency: \"< 500ms\"\n";
    }

    cppload::scenario::ScenarioEngine engine(config_path.string());
    ASSERT_TRUE(engine.load_config());
    ASSERT_TRUE(engine.validate());

    std::atomic<int> total{0};
    std::atomic<int> errors{0};
    engine.run([&](const cppload::scenario::HttpStep&,
                   const cppload::net::Response& resp,
                   cppload::metrics::MetricsCollector&) {
        total.fetch_add(1, std::memory_order_relaxed);
        if (resp.status_code != 200) {
            errors.fetch_add(1, std::memory_order_relaxed);
        }
    });

    std::remove(config_path.string().c_str());

    EXPECT_GT(total.load(), 250);
    EXPECT_LT(total.load(), 5000);
    EXPECT_EQ(errors.load(), 0);
}

// TR-5.1: An empty load_profile must be replaced with a default stage derived
// from the CLI/engine set rps + duration, so the run still issues requests.
TEST(ScenarioEngineIntegrationTest, EmptyLoadProfileUsesDefaultStage) {
    MockHttpServer server;
    server.set_handler([](const auto&) {
        http::response<http::string_body> res;
        res.result(http::status::ok);
        res.set(http::field::content_type, "text/plain");
        res.body() = "OK";
        res.prepare_payload();
        return res;
    });
    ASSERT_TRUE(server.start());

    auto config_path =
        std::filesystem::temp_directory_path() / "cppload_engine_default_stage.yaml";
    {
        std::ofstream f(config_path);
        f << "version: \"1.0\"\n"
          << "test_id: \"engine-int-default\"\n"
          << "target:\n"
          << "  base_url: http://127.0.0.1:" << server.port() << "\n"
          << "  protocol: http1.1\n"
          << "scenarios:\n"
          << "  - name: default\n"
          << "    weight: 100\n"
          << "    steps:\n"
          << "      - http:\n"
          << "          method: GET\n"
          << "          path: /default-stage\n"
          << "          assertions:\n"
          << "            - status_code == 200\n";
    }

    cppload::scenario::ScenarioEngine engine(config_path.string());
    ASSERT_TRUE(engine.load_config());
    ASSERT_TRUE(engine.validate());
    ASSERT_TRUE(engine.config().load_profile.stages.empty());
    engine.set_max_duration(std::chrono::seconds(3));
    engine.set_target_rps(50);

    std::atomic<int> total{0};
    engine.run([&](const cppload::scenario::HttpStep&,
                   const cppload::net::Response& resp,
                   cppload::metrics::MetricsCollector&) {
        if (resp.status_code == 200) {
            total.fetch_add(1, std::memory_order_relaxed);
        }
    });

    std::remove(config_path.string().c_str());

    EXPECT_GT(total.load(), 0);
}

// TR-5.2: concurrent_users=0 in YAML must be clamped to a sane default, never
// divide by zero or build zero workers.
TEST(ScenarioEngineIntegrationTest, ZeroConcurrentUsersClamped) {
    MockHttpServer server;
    server.set_handler([](const auto&) {
        http::response<http::string_body> res;
        res.result(http::status::ok);
        res.set(http::field::content_type, "text/plain");
        res.body() = "OK";
        res.prepare_payload();
        return res;
    });
    ASSERT_TRUE(server.start());

    auto config_path =
        std::filesystem::temp_directory_path() / "cppload_engine_zero_users.yaml";
    {
        std::ofstream f(config_path);
        f << "version: \"1.0\"\n"
          << "test_id: \"engine-int-zero-users\"\n"
          << "target:\n"
          << "  base_url: http://127.0.0.1:" << server.port() << "\n"
          << "  protocol: http1.1\n"
          << "load_profile:\n"
          << "  - stage: sustain\n"
          << "    duration: 2s\n"
          << "    target_rps: 100\n"
          << "    concurrent_users: 0\n"
          << "scenarios:\n"
          << "  - name: default\n"
          << "    weight: 100\n"
          << "    steps:\n"
          << "      - http:\n"
          << "          method: GET\n"
          << "          path: /zero-users\n"
          << "          assertions:\n"
          << "            - status_code == 200\n";
    }

    cppload::scenario::ScenarioEngine engine(config_path.string());
    ASSERT_TRUE(engine.load_config());
    ASSERT_TRUE(engine.validate());
    ASSERT_FALSE(engine.run_failed());

    std::atomic<int> total{0};
    engine.run([&](const cppload::scenario::HttpStep&,
                   const cppload::net::Response& resp,
                   cppload::metrics::MetricsCollector&) {
        if (resp.status_code == 200) {
            total.fetch_add(1, std::memory_order_relaxed);
        }
    });

    std::remove(config_path.string().c_str());

    EXPECT_FALSE(engine.run_failed());
    EXPECT_GT(total.load(), 0);
}
