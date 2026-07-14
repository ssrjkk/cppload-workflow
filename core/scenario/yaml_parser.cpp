#include "cppload/scenario/engine.hpp"
#include <yaml-cpp/yaml.h>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <chrono>
#include <cctype>
#include <cstdlib>

namespace cppload::scenario {

namespace {

std::chrono::seconds parse_duration(const std::string& str) {
    if (str.empty()) return std::chrono::seconds{0};
    char unit = str.back();
    std::string num_str = str.substr(0, str.length() - 1);
    long long value = 0;
    try {
        value = std::stoll(num_str);
    } catch (const std::exception&) {
        return std::chrono::seconds{0};
    }
    switch (unit) {
        case 's': return std::chrono::seconds{value};
        case 'm': return std::chrono::minutes{value};
        case 'h': return std::chrono::hours{value};
        default:
            std::cerr << "Warning: unrecognized duration unit '" << unit
                      << "' in \"" << str << "\", treating as 0s" << std::endl;
            return std::chrono::seconds{0};
    }
}

double parse_error_rate(const std::string& str) {
    auto start = str.find_first_of("0123456789");
    auto end = str.find("%");
    if (start != std::string::npos && end != std::string::npos) {
        try {
            return std::stod(str.substr(start, end - start));
        } catch (const std::exception&) {
            return 0.1;
        }
    }
    return 0.1;
}

std::chrono::milliseconds parse_latency(const std::string& str) {
    auto start = str.find_first_of("0123456789");
    if (start == std::string::npos) return std::chrono::milliseconds{500};
    size_t num_end = start;
    while (num_end < str.length() && std::isdigit(static_cast<unsigned char>(str[num_end]))) ++num_end;
    long long value = 0;
    try {
        value = std::stoll(str.substr(start, num_end - start));
    } catch (const std::exception&) {
        return std::chrono::milliseconds{500};
    }
    if (str.find("ms") != std::string::npos) return std::chrono::milliseconds{value};
    if (str.find("s") != std::string::npos) return std::chrono::seconds{value};
    return std::chrono::milliseconds{value};
}

void substitute_env(YAML::Node node) {
    if (!node.IsDefined()) return;
    if (node.IsScalar()) {
        std::string val = node.Scalar();
        std::string result;
        size_t pos = 0;
        while (pos < val.length()) {
            auto dollar = val.find("${", pos);
            if (dollar == std::string::npos) {
                result += val.substr(pos);
                break;
            }
            result += val.substr(pos, dollar - pos);
            auto end = val.find("}", dollar);
            if (end == std::string::npos) {
                result += val.substr(dollar);
                break;
            }
            std::string expr = val.substr(dollar + 2, end - dollar - 2);
            auto colon = expr.find(":-");
            std::string var_name = expr.substr(0, colon);
            std::string default_val = (colon != std::string::npos) ? expr.substr(colon + 2) : "";
            const char* env_val = std::getenv(var_name.c_str());
            result += env_val ? env_val : default_val;
            pos = end + 1;
        }
        node = result;
    } else if (node.IsMap()) {
        for (auto it = node.begin(); it != node.end(); ++it) {
            substitute_env(it->second);
        }
    } else if (node.IsSequence()) {
        for (auto it = node.begin(); it != node.end(); ++it) {
            substitute_env(*it);
        }
    }
}

} // anonymous namespace

bool parse_config_file(const std::string& path, ScenarioConfig& config, std::string& error) {
    std::ifstream file(path);
    if (!file) {
        error = "Cannot open config file: " + path;
        return false;
    }

    YAML::Node root;
    try {
        root = YAML::Load(file);
    } catch (const YAML::Exception& e) {
        error = std::string("YAML parse error: ") + e.what();
        return false;
    }

    substitute_env(root);

    if (root["version"]) config.version = root["version"].as<std::string>();
    if (root["test_id"]) config.test_id = root["test_id"].as<std::string>();

    if (root["target"]) {
        auto target = root["target"];
        if (target["base_url"]) config.target.base_url = target["base_url"].as<std::string>();
        if (target["protocol"]) config.target.protocol = target["protocol"].as<std::string>();
        if (target["tls"] && target["tls"]["verify"]) {
            config.target.tls.verify = target["tls"]["verify"].as<bool>();
        }
    }

    if (root["load_profile"] && root["load_profile"].IsSequence()) {
        for (const auto& st : root["load_profile"]) {
            LoadProfile::Stage stage;
            if (st["stage"]) stage.name = st["stage"].as<std::string>();
            if (st["duration"]) stage.duration = parse_duration(st["duration"].as<std::string>());
            if (st["target_rps"]) stage.target_rps = st["target_rps"].as<uint32_t>();
            if (st["concurrent_users"]) stage.concurrent_users = st["concurrent_users"].as<uint32_t>();
            config.load_profile.stages.push_back(std::move(stage));
        }
    }

    if (root["scenarios"] && root["scenarios"].IsSequence()) {
        for (const auto& sc : root["scenarios"]) {
            Scenario scenario;
            if (sc["name"]) scenario.name = sc["name"].as<std::string>();
            if (sc["weight"]) scenario.weight = sc["weight"].as<uint32_t>();

            if (sc["steps"] && sc["steps"].IsSequence()) {
                for (const auto& step_node : sc["steps"]) {
                    if (!step_node["http"]) continue;
                    auto http = step_node["http"];
                    HttpStep step;
                    if (http["method"]) step.method = http["method"].as<std::string>();
                    if (http["path"]) step.path = http["path"].as<std::string>();
                    if (http["body"]) {
                        auto body_node = http["body"];
                        if (body_node.IsScalar()) {
                            step.body = body_node.as<std::string>();
                        } else {
                            std::stringstream ss;
                            ss << body_node;
                            step.body = ss.str();
                        }
                    }
                    if (http["headers"] && http["headers"].IsMap()) {
                        for (const auto& h : http["headers"]) {
                            step.headers[h.first.as<std::string>()] = h.second.as<std::string>();
                        }
                    }
                    if (http["assertions"] && http["assertions"].IsSequence()) {
                        for (const auto& a : http["assertions"]) {
                            step.assertions.push_back(a.as<std::string>());
                        }
                    }
                    scenario.steps.push_back(std::move(step));
                }
            }

            config.scenarios.push_back(std::move(scenario));
        }
    }

    if (root["authentication"]) {
        auto auth = root["authentication"];
        if (auth["type"]) config.authentication.type = auth["type"].as<std::string>();
        if (auth["token_endpoint"]) config.authentication.token_endpoint = auth["token_endpoint"].as<std::string>();
        if (auth["client_credentials"]) {
            auto cc = auth["client_credentials"];
            if (cc["client_id"]) config.authentication.client_credentials.client_id = cc["client_id"].as<std::string>();
            if (cc["client_secret"]) config.authentication.client_credentials.client_secret = cc["client_secret"].as<std::string>();
        }
    }

    if (root["observability"]) {
        auto obs = root["observability"];
        if (obs["metrics"] && obs["metrics"]["prometheus"]) {
            auto pm = obs["metrics"]["prometheus"];
            if (pm["enabled"]) config.observability.metrics.prometheus.enabled = pm["enabled"].as<bool>();
            if (pm["port"]) config.observability.metrics.prometheus.port = pm["port"].as<uint16_t>();
        }
        if (obs["tracing"]) {
            auto tr = obs["tracing"];
            if (tr["otlp_endpoint"]) config.observability.tracing.otlp_endpoint = tr["otlp_endpoint"].as<std::string>();
            if (tr["sample_rate"]) config.observability.tracing.sample_rate = tr["sample_rate"].as<double>();
        }
        if (obs["logging"]) {
            auto lg = obs["logging"];
            if (lg["level"]) config.observability.logging.level = lg["level"].as<std::string>();
            if (lg["format"]) config.observability.logging.format = lg["format"].as<std::string>();
        }
    }

    if (root["sla"]) {
        auto sla = root["sla"];
        if (sla["error_rate"]) {
            config.sla.max_error_rate = parse_error_rate(sla["error_rate"].as<std::string>());
        }
        if (sla["p99_latency"]) {
            config.sla.max_p99_latency = parse_latency(sla["p99_latency"].as<std::string>());
        }
    }

    return true;
}

} // namespace cppload::scenario
