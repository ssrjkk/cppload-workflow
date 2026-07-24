// @author ssrjkk | cppload
#include <gtest/gtest.h>
#include "cppload/scenario/engine.hpp"
#include <filesystem>
#include <fstream>
#include <cstdio>

class YamlParserTest : public ::testing::Test {
protected:
    void SetUp() override {
        auto tmp = std::filesystem::temp_directory_path();
        test_file = (tmp / "cppload_test_config.yaml").string();
        bad_file = (tmp / "cppload_test_bad_yaml.yaml").string();

        // Create a minimal valid YAML config
        std::ofstream f(test_file);
        f << "version: \"1.0\"\n"
          << "test_id: \"unit-test\"\n"
          << "target:\n"
          << "  base_url: http://localhost:8080\n"
          << "  protocol: http1.1\n"
          << "load_profile:\n"
          << "  - stage: rampup\n"
          << "    duration: 1m\n"
          << "    target_rps: 100\n"
          << "scenarios:\n"
          << "  - name: test_scenario\n"
          << "    weight: 100\n"
          << "    steps:\n"
          << "      - http:\n"
          << "          method: GET\n"
          << "          path: \"/api/test\"\n"
          << "sla:\n"
          << "  error_rate: \"< 1%\"\n"
          << "  p99_latency: \"< 500ms\"\n";
        f.close();
    }

    void TearDown() override {
        std::remove(test_file.c_str());
        std::remove(bad_file.c_str());
    }

    std::string test_file = "test_config_tmp.yaml";
    std::string bad_file = "bad_yaml_tmp.yaml";
};

TEST_F(YamlParserTest, LoadsConfig) {
    cppload::scenario::ScenarioEngine engine(test_file);
    EXPECT_TRUE(engine.load_config());
    EXPECT_TRUE(engine.validate());
}

TEST_F(YamlParserTest, ReadsTestId) {
    cppload::scenario::ScenarioEngine engine(test_file);
    ASSERT_TRUE(engine.load_config());
    EXPECT_EQ(engine.config().test_id, "unit-test");
}

TEST_F(YamlParserTest, ReadsTarget) {
    cppload::scenario::ScenarioEngine engine(test_file);
    ASSERT_TRUE(engine.load_config());
    EXPECT_EQ(engine.config().target.base_url, "http://localhost:8080");
    EXPECT_EQ(engine.config().target.protocol, "http1.1");
}

TEST_F(YamlParserTest, ReadsLoadProfile) {
    cppload::scenario::ScenarioEngine engine(test_file);
    ASSERT_TRUE(engine.load_config());
    ASSERT_EQ(engine.config().load_profile.stages.size(), 1);
    EXPECT_EQ(engine.config().load_profile.stages[0].name, "rampup");
    EXPECT_EQ(engine.config().load_profile.stages[0].target_rps, 100);
}

TEST_F(YamlParserTest, ReadsScenario) {
    cppload::scenario::ScenarioEngine engine(test_file);
    ASSERT_TRUE(engine.load_config());
    ASSERT_EQ(engine.config().scenarios.size(), 1);
    EXPECT_EQ(engine.config().scenarios[0].name, "test_scenario");
    EXPECT_EQ(engine.config().scenarios[0].weight, 100);
    ASSERT_EQ(engine.config().scenarios[0].steps.size(), 1);
    EXPECT_EQ(engine.config().scenarios[0].steps[0].method, "GET");
    EXPECT_EQ(engine.config().scenarios[0].steps[0].path, "/api/test");
}

TEST_F(YamlParserTest, ReadsSLA) {
    cppload::scenario::ScenarioEngine engine(test_file);
    ASSERT_TRUE(engine.load_config());
    EXPECT_DOUBLE_EQ(engine.config().sla.max_error_rate, 1.0);
    EXPECT_EQ(engine.config().sla.max_p99_latency.count(), 500);
}

TEST_F(YamlParserTest, MissingFile) {
    cppload::scenario::ScenarioEngine engine("nonexistent.yaml");
    EXPECT_FALSE(engine.load_config());
    EXPECT_FALSE(engine.last_error().empty());
}

TEST_F(YamlParserTest, InvalidYaml) {
    std::ofstream f(bad_file);
    f << ": : invalid yaml :\n";
    f.close();
    cppload::scenario::ScenarioEngine engine(bad_file);
    EXPECT_FALSE(engine.load_config());
}

TEST_F(YamlParserTest, SetTargetRps) {
    cppload::scenario::ScenarioEngine engine(test_file);
    engine.load_config();
    engine.set_target_rps(500);
    EXPECT_EQ(engine.target_rps(), 500);
}

TEST_F(YamlParserTest, CheckSlaDefault) {
    cppload::scenario::ScenarioEngine engine(test_file);
    engine.load_config();
    cppload::metrics::MetricsCollector metrics;
    // No requests - should pass
    EXPECT_TRUE(engine.check_sla(metrics));
}