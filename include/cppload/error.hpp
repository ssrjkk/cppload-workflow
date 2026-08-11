// @author ssrjkk | cppload
#pragma once

#include <system_error>
#include <string>
#include <type_traits>

namespace cppload {

enum class Err {
    success = 0,
    invalid_method,
    invalid_target,
    dns_failure,
    connection_refused,
    connection_reset,
    connection_timeout,
    read_error,
    write_error,
    tls_handshake_failed,
    tls_verify_failed,
    protocol_violation,
    invalid_response,
    operation_cancelled,
    timeout,
    invalid_config,
    parse_error,
    auth_failed,
    not_implemented,
    unknown,
    // Vault-specific
    vault_not_found,
    vault_permission_denied,
    vault_token_invalid,
    vault_server_error,
    vault_unreachable,
    // Auth-specific
    auth_token_expired,
    auth_invalid_grant,
    auth_invalid_client,
    auth_server_error,
    auth_parse_error,
};

// Pin the numeric values so test expectations and any external consumers of
// the numeric codes break loudly if the enum order ever drifts.
static_assert(static_cast<int>(Err::success) == 0,
    "Err::success must remain 0");
static_assert(static_cast<int>(Err::timeout) == 14,
    "Err::timeout numeric code drift detected");
static_assert(static_cast<int>(Err::invalid_config) == 15,
    "Err::invalid_config numeric code drift detected");
static_assert(static_cast<int>(Err::vault_not_found) == 20,
    "Err::vault_not_found numeric code drift detected");
static_assert(static_cast<int>(Err::auth_parse_error) == 29,
    "Err::auth_parse_error numeric code drift detected");

class ErrCategory : public std::error_category {
public:
    const char* name() const noexcept override {
        return "cppload";
    }

    std::string message(int ev) const override {
        switch (static_cast<Err>(ev)) {
            case Err::success: return "success";
            case Err::invalid_method: return "invalid HTTP method";
            case Err::invalid_target: return "invalid request target";
            case Err::dns_failure: return "DNS resolution failed";
            case Err::connection_refused: return "connection refused";
            case Err::connection_reset: return "connection reset";
            case Err::connection_timeout: return "connection timeout";
            case Err::read_error: return "read error";
            case Err::write_error: return "write error";
            case Err::tls_handshake_failed: return "TLS handshake failed";
            case Err::tls_verify_failed: return "TLS certificate verification failed";
            case Err::protocol_violation: return "protocol violation";
            case Err::invalid_response: return "invalid response";
            case Err::operation_cancelled: return "operation cancelled";
            case Err::timeout: return "timeout";
            case Err::invalid_config: return "invalid configuration";
            case Err::parse_error: return "parse error";
            case Err::auth_failed: return "authentication failed";
            case Err::not_implemented: return "not implemented";
            case Err::unknown: return "unknown error";
            case Err::vault_not_found: return "vault: secret not found";
            case Err::vault_permission_denied: return "vault: permission denied";
            case Err::vault_token_invalid: return "vault: invalid token";
            case Err::vault_server_error: return "vault: server error";
            case Err::vault_unreachable: return "vault: server unreachable";
            case Err::auth_token_expired: return "auth: token expired";
            case Err::auth_invalid_grant: return "auth: invalid grant";
            case Err::auth_invalid_client: return "auth: invalid client";
            case Err::auth_server_error: return "auth: server error";
            case Err::auth_parse_error: return "auth: response parse error";
        }
        return "unrecognized error";
    }
};

[[nodiscard]] inline const std::error_category& err_category() {
    static ErrCategory category;
    return category;
}

[[nodiscard]] inline std::error_code make_error_code(Err e) {
    return std::error_code(static_cast<int>(e), err_category());
}

[[nodiscard]] inline std::error_condition make_error_condition(Err e) {
    return std::error_condition(static_cast<int>(e), err_category());
}

} // namespace cppload

namespace std {
    template <>
    struct is_error_code_enum<cppload::Err> : true_type {};
}