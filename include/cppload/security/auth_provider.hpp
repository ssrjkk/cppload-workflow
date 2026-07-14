#pragma once

#include <string>
#include <memory>
#include <unordered_map>

namespace cppload::security {

enum class AuthType {
    NONE,
    API_KEY,
    BEARER_TOKEN,
    OAUTH2,
    MTLS
};

struct AuthConfig {
    AuthType type{AuthType::NONE};
    std::string api_key;
    std::string token;
    std::string client_id;
    std::string client_secret;
    std::string token_endpoint;
    std::string cert_path;
    std::string key_path;
    std::string ca_path;
};

class AuthProvider {
public:
    explicit AuthProvider(const AuthConfig& config = {});
    ~AuthProvider();
    
    void apply_headers(std::unordered_map<std::string, std::string>& headers);
    [[nodiscard]] std::string get_auth_header() const;
    
    bool refresh_token();
    [[nodiscard]] bool is_expired() const;
    
private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace cppload::security
