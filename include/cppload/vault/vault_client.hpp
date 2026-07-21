#pragma once

#include "cppload/result.hpp"
#include "cppload/error.hpp"
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

    [[nodiscard]] Result<std::string, Err> get_secret(
        const std::string& path,
        const std::string& key);

    [[nodiscard]] Result<std::unordered_map<std::string, std::string>, Err>
    get_secret_map(const std::string& path);

    [[nodiscard]] Result<bool, Err> put_secret(
        const std::string& path,
        const std::unordered_map<std::string, std::string>& data);

    [[nodiscard]] Result<std::string, Err> get_kv_secret(
        const std::string& path,
        const std::string& key);

    [[nodiscard]] Result<std::string, Err> get_database_creds(
        const std::string& role_name);

    [[nodiscard]] Result<std::string, Err> get_approle_token(
        const std::string& role_id,
        const std::string& secret_id);

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace cppload::vault
