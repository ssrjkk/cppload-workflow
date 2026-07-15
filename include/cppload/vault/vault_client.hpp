#pragma once

#include <string>
#include <memory>
#include <unordered_map>

namespace cppload::vault {

struct VaultConfig {
    std::string address{"http://127.0.0.1:8200"};
    std::string token;
    std::string engine_path{"secret"};
    int timeout_seconds{5};
};

class VaultClient {
public:
    explicit VaultClient(const VaultConfig& config = {});
    ~VaultClient();
    
    VaultClient(const VaultClient&) = delete;
    VaultClient& operator=(const VaultClient&) = delete;
    
    [[nodiscard]] bool is_connected() const;
    
    [[nodiscard]] std::string get_secret(const std::string& path, 
                         const std::string& key);
    
    [[nodiscard]] std::unordered_map<std::string, std::string> get_secret_map(
        const std::string& path);
    
    [[nodiscard]] bool put_secret(const std::string& path,
                   const std::unordered_map<std::string, std::string>& data);
    
    [[nodiscard]] std::string get_kv_secret(const std::string& path, 
                             const std::string& key);
    
    [[nodiscard]] std::string get_database_creds(const std::string& role_name);
    
    [[nodiscard]] std::string get_approle_token(const std::string& role_id,
                                 const std::string& secret_id);
    
    [[nodiscard]] std::string last_error() const;
    
private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace cppload::vault
