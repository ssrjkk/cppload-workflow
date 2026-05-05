#include "cppload/scenario/engine.hpp"
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <algorithm>
#include <cctype>

namespace cppload::scenario {

// Simple YAML parser (replace with yaml-cpp in production)
static std::string trim(const std::string& s) {
    auto start = std::find_if_not(s.begin(), s.end(), [](unsigned char c){ return std::isspace(c); });
    auto end = std::find_if_not(s.rbegin(), s.rend(), [](unsigned char c){ return std::isspace(c); }).base();
    return (start < end) ? std::string(start, end) : std::string();
}

static std::string extract_value(const std::string& content, const std::string& key) {
    auto pos = content.find(key + ":");
    if (pos == std::string::npos) return "";
    
    auto line_start = pos + key.length() + 1;
    auto line_end = content.find("\n", line_start);
    if (line_end == std::string::npos) line_end = content.length();
    
    std::string value = content.substr(line_start, line_end - line_start);
    
    // Remove quotes and whitespace
    value = trim(value);
    if (!value.empty() && value[0] == '"') {
        auto end_quote = value.rfind('"');
        if (end_quote != 0) {
            value = value.substr(1, end_quote - 1);
        }
    }
    
    // Handle environment variables ${VAR:-default}
    if (value.find("${") == 0) {
        auto end = value.find("}");
        if (end != std::string::npos) {
            std::string var_expr = value.substr(2, end - 2);
            auto colon = var_expr.find(":-");
            if (colon != std::string::npos) {
                std::string var_name = var_expr.substr(0, colon);
                std::string default_val = var_expr.substr(colon + 2);
                
                const char* env_val = std::getenv(var_name.c_str());
                return env_val ? env_val : default_val;
            }
        }
    }
    
    return value;
}

static LoadProfile parse_load_profile(const std::string& content) {
    LoadProfile profile;
    // Simplified parsing - in production use yaml-cpp
    return profile;
}

bool ScenarioEngine::Impl::load_config() {
    std::ifstream file(config_path_);
    if (!file) {
        last_error_ = "Cannot open config file: " + config_path_;
        return false;
    }
    
    std::string content((std::istreambuf_iterator<char>(file)),
                       std::istreambuf_iterator<char>());
    
    config_.version = extract_value(content, "version");
    config_.test_id = extract_value(content, "test_id");
    config_.target.base_url = extract_value(content, "base_url");
    config_.target.protocol = extract_value(content, "protocol");
    
    if (config_.target.protocol.empty()) {
        config_.target.protocol = "http1.1";
    }
    
    // Parse SLA
    auto sla_section = content.substr(content.find("sla:"));
    auto sla_end = sla_section.find("\n\n");
    if (sla_end != std::string::npos) {
        sla_section = sla_section.substr(0, sla_end);
    }
    
    auto error_rate_str = extract_value(sla_section, "error_rate");
    if (!error_rate_str.empty()) {
        // Parse "< 0.1%" format
        try {
            auto start = error_rate_str.find_first_of("0123456789");
            auto end = error_rate_str.find("%");
            if (start != std::string::npos && end != std::string::npos) {
                std::string num = error_rate_str.substr(start, end - start);
                config_.sla.max_error_rate = std::stod(num);
            }
        } catch(...) {}
    }
    
    return true;
}

} // namespace cppload::scenario
