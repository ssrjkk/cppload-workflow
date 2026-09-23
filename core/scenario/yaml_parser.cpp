// @author ssrjkk | cppload
#include "cppload/scenario/engine.hpp"
#include "cppload/core/constants.hpp"
#include <yaml-cpp/yaml.h>
#include <array>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <chrono>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <algorithm>
#include <mutex>
#include <limits>

namespace cppload::scenario {

namespace {

// Scales a parsed numeric value into milliseconds with overflow clamping.
// A magnitude beyond the representable range (LLONG_MAX ms) is capped instead
// of being converted through duration_cast, which would have been UB when the
// scaled value overflowed int64 (e.g. seconds{LLONG_MAX} -> milliseconds).
std::chrono::milliseconds scale_to_ms(double value, double multiplier) {
    double ms = value * multiplier;
    if (ms < 0.0) return std::chrono::milliseconds{0};
    if (ms >= 9.0e15) return std::chrono::milliseconds::max();
    return std::chrono::milliseconds{static_cast<int64_t>(ms)};
}

double parse_number(const std::string& str) {
    try {
        return std::stod(str);
    } catch (const std::exception&) {
        return -1.0;
    }
}

std::chrono::milliseconds parse_duration(const std::string& str) {
    if (str.empty()) return std::chrono::milliseconds{0};
    // Try multi-character suffixes first
    if (str.size() >= 3 && str.substr(str.size() - 3) == "min") {
        return scale_to_ms(parse_number(str.substr(0, str.size() - 3)), 60000.0);
    }
    if (str.size() >= 2 && str.substr(str.size() - 2) == "ms") {
        return scale_to_ms(parse_number(str.substr(0, str.size() - 2)), 1.0);
    }
    char unit = str.back();
    double value = parse_number(str.substr(0, str.length() - 1));
    if (value < 0.0) return std::chrono::milliseconds{0};
    switch (unit) {
        case 's': return scale_to_ms(value, 1000.0);
        case 'm': return scale_to_ms(value, 60000.0);
        case 'h': return scale_to_ms(value, 3600000.0);
        default:
            std::cerr << "Warning: unrecognized duration unit '" << unit
                      << "' in \"" << str << "\", treating as 0s\n";
            return std::chrono::milliseconds{0};
    }
}

double parse_error_rate(const std::string& str) {
    auto start = str.find_first_of("0123456789");
    auto end = str.find('%');
    if (start != std::string::npos && end != std::string::npos) {
        try {
            return std::stod(str.substr(start, end - start));
        } catch (const std::exception&) {
            return core::kDefaultErrorRate;
        }
    }
    return core::kDefaultErrorRate;
}

// Parses "N[k|m|g]" (case-insensitive suffix) into a byte count. Bare numbers
// are treated as bytes. Returns 0 on an unparseable input so the engine falls
// back to its default cap.
size_t parse_bytesize(const std::string& str) {
    if (str.empty()) return 0;
    size_t num_end = 0;
    while (num_end < str.length() &&
           std::isdigit(static_cast<unsigned char>(str[num_end]))) ++num_end;
    if (num_end == 0) return 0;
    double value = 0.0;
    try {
        value = std::stod(str.substr(0, num_end));
    } catch (const std::exception&) {
        return 0;
    }
    if (value < 0.0) return 0;
    std::string unit = str.substr(num_end);
    std::transform(unit.begin(), unit.end(), unit.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    double multiplier = 1.0;
    if (unit == "k" || unit == "kb") multiplier = 1024.0;
    else if (unit == "m" || unit == "mb") multiplier = 1024.0 * 1024.0;
    else if (unit == "g" || unit == "gb") multiplier = 1024.0 * 1024.0 * 1024.0;
    if (value * multiplier > static_cast<double>(std::numeric_limits<size_t>::max())) {
        return std::numeric_limits<size_t>::max();
    }
    return static_cast<size_t>(value * multiplier);
}

std::chrono::milliseconds parse_latency(const std::string& str) {
    auto start = str.find_first_of("0123456789");
    if (start == std::string::npos) return core::kDefaultLatencyMs;
    size_t num_end = start;
    while (num_end < str.length() && std::isdigit(static_cast<unsigned char>(str[num_end]))) ++num_end;
    double value = 0.0;
    try {
        value = std::stod(str.substr(start, num_end - start));
    } catch (const std::exception&) {
        return core::kDefaultLatencyMs;
    }
    if (str.find("ms") != std::string::npos) return scale_to_ms(value, 1.0);
    if (str.find('s') != std::string::npos) return scale_to_ms(value, 1000.0);
    return scale_to_ms(value, 1.0);
}

// Substitutes ${VAR} and ${VAR:-default} occurrences in all scalar leaves.
// Returns (via out) the first variable that had no default and no value, so
// callers can turn a silently-empty result into a precise config error.
void substitute_env(YAML::Node node, std::string& first_missing) {
    static std::mutex env_mtx;
    if (!node.IsDefined()) return;
    if (node.IsScalar()) {
        std::string val = node.Scalar();
        std::string result;
        result.reserve(val.size());
        size_t pos = 0;
        while (pos < val.length()) {
            auto dollar = val.find("${", pos);
            if (dollar == std::string::npos) {
                result += val.substr(pos);
                break;
            }
            result += val.substr(pos, dollar - pos);
            auto end = val.find('}', dollar + 2);
            if (end == std::string::npos) {
                result += val.substr(dollar);
                break;
            }
            std::string expr = val.substr(dollar + 2, end - dollar - 2);
            auto colon = expr.find(":-");
            std::string var_name = expr.substr(0, colon);
            std::string default_val = (colon != std::string::npos) ? expr.substr(colon + 2) : "";
            bool valid_name = !var_name.empty() &&
                std::all_of(var_name.begin(), var_name.end(),
                    [](char c) { return std::isalnum(c) || c == '_'; });
            std::string env_val;
            if (valid_name) {
                std::lock_guard<std::mutex> lock(env_mtx);
                const char* env_raw = std::getenv(var_name.c_str());
                if (env_raw) env_val = env_raw;
                // Unset variables without a default become a config error the
                // caller can report instead of silently resolving to "".
                if (env_val.empty() && colon == std::string::npos &&
                    first_missing.empty()) {
                    first_missing = var_name;
                }
            }
            result += env_val.empty() ? default_val : env_val;
            pos = end + 1;
        }
        if (result != val) {
            node = result;
        }
    } else if (node.IsMap()) {
        for (auto it = node.begin(); it != node.end(); ++it) {
            substitute_env(it->second, first_missing);
        }
    } else if (node.IsSequence()) {
        for (auto it = node.begin(); it != node.end(); ++it) {
            substitute_env(*it, first_missing);
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

    std::string missing_var;
    substitute_env(root, missing_var);

    // Use a const view for lookups so operator[] never inserts missing keys
    // into the document while validating.
    const YAML::Node& root_view = root;
    if (!root_view.IsMap()) {
        error = "YAML root must be a mapping";
        return false;
    }
    if (!root_view["target"]) {
        error = "target section is required";
        return false;
    }

    try {
        const YAML::Node& target = root_view["target"];
        if (!target.IsMap()) {
            error = "target must be a mapping";
            return false;
        }

        if (root_view["version"]) config.version = root_view["version"].as<std::string>();
        if (root_view["test_id"]) config.test_id = root_view["test_id"].as<std::string>();

        if (target["base_url"]) config.target.base_url = target["base_url"].as<std::string>();
        if (target["protocol"]) config.target.protocol = target["protocol"].as<std::string>();
        if (target["tls"] && target["tls"]["verify"]) {
            config.target.tls.verify = target["tls"]["verify"].as<bool>();
        }
        if (target["max_body_bytes"]) {
            config.target.max_body_bytes = parse_bytesize(target["max_body_bytes"].as<std::string>());
        }
        // An unresolvable ${VAR} (no default) that left the base URL empty is a
        // configuration error with a precise message, not a silent empty URL.
        if (!missing_var.empty() && config.target.base_url.empty()) {
            error = "target.base_url is empty; environment variable \"${" +
                missing_var + "}\" is not set and has no default";
            return false;
        }
    } catch (const YAML::Exception& e) {
        error = std::string("YAML target parse error: ") + e.what();
        return false;
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
                            if (a.IsScalar()) {
                                step.assertions.push_back(a.as<std::string>());
                            }
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

namespace {

bool check_assertion_format(const std::string& expr, std::string& err) {
    if (expr.empty()) {
        err = "empty assertion";
        return false;
    }
    size_t i = 0;
    while (i < expr.size() && (std::isalnum(static_cast<unsigned char>(expr[i])) || expr[i] == '_')) {
        ++i;
    }
    if (i == 0) {
        err = "missing left-hand side identifier";
        return false;
    }
    size_t op_start = i;
    while (i < expr.size() && std::isspace(static_cast<unsigned char>(expr[i]))) ++i;
    size_t j = i;
    while (j < expr.size() && (expr[j] == '=' || expr[j] == '!' || expr[j] == '<' || expr[j] == '>' || std::isspace(static_cast<unsigned char>(expr[j])))) {
        ++j;
    }
    if (j == i) {
        err = "missing operator (==, !=, >=, <=, >, <)";
        return false;
    }
    std::string op_token;
    for (size_t k = i; k < j; ++k) {
        if (!std::isspace(static_cast<unsigned char>(expr[k]))) op_token.push_back(expr[k]);
    }
    static constexpr std::array<std::string_view, 6> kValid{"==","!=",">=","<=",">","<"};
    bool op_ok = false;
    for (auto v : kValid) if (op_token == v) { op_ok = true; break; }
    if (!op_ok) {
        err = "invalid operator '" + op_token + "', expected one of ==, !=, >=, <=, >, <";
        return false;
    }
    (void)op_start;
    return true;
}

} // anonymous namespace

bool validate_scenario_config(const ScenarioConfig& cfg, std::string& error) {
    if (cfg.test_id.empty()) {
        error = "field 'test_id' is required (string). Example: test_id: \"my-load-test-2026\"";
        return false;
    }
    if (cfg.target.base_url.empty()) {
        error = "field 'target.base_url' is required (string). Example: base_url: \"http://localhost:8080\"";
        return false;
    }
    if (cfg.scenarios.empty()) {
        error = "field 'scenarios' must contain at least one scenario";
        return false;
    }
    for (size_t si = 0; si < cfg.scenarios.size(); ++si) {
        const auto& s = cfg.scenarios[si];
        if (s.weight > 1000) {
            error = "field 'scenarios[" + std::to_string(si) + "].weight' out of range: "
                    + std::to_string(s.weight) + " expected 0..1000. Example: weight: 70";
            return false;
        }
        if (s.steps.empty()) {
            error = "scenario '" + s.name + "' (index " + std::to_string(si) +
                    ") has no steps; add at least one http step";
            return false;
        }
        for (size_t sti = 0; sti < s.steps.size(); ++sti) {
            const auto& step = s.steps[sti];
            if (step.method.empty()) {
                error = "field 'scenarios[" + std::to_string(si) + "].steps[" + std::to_string(sti) +
                        "].http.method' is required. Example: method: GET";
                return false;
            }
            if (step.path.empty()) {
                error = "field 'scenarios[" + std::to_string(si) + "].steps[" + std::to_string(sti) +
                        "].http.path' is required. Example: path: \"/api/v1/health\"";
                return false;
            }
            for (size_t ai = 0; ai < step.assertions.size(); ++ai) {
                std::string aerr;
                if (!check_assertion_format(step.assertions[ai], aerr)) {
                    error = "field 'scenarios[" + std::to_string(si) + "].steps[" + std::to_string(sti) +
                            "].assertions[" + std::to_string(ai) + "] " + aerr +
                            ". Example: status_code == 201";
                    return false;
                }
            }
        }
    }
    for (size_t li = 0; li < cfg.load_profile.stages.size(); ++li) {
        const auto& st = cfg.load_profile.stages[li];
        if (st.target_rps > 1000000) {
            error = "field 'load_profile[" + std::to_string(li) + "].target_rps' out of range: "
                    + std::to_string(st.target_rps) + " expected 0..1000000";
            return false;
        }
        if (st.concurrent_users > 100000) {
            error = "field 'load_profile[" + std::to_string(li) + "].concurrent_users' out of range: "
                    + std::to_string(st.concurrent_users) + " expected 0..100000";
            return false;
        }
    }
    return true;
}

} // namespace cppload::scenario