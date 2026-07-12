#pragma once

#include <system_error>
#include <string>

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
};

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
        }
        return "unrecognized error";
    }
};

inline const std::error_category& err_category() {
    static ErrCategory category;
    return category;
}

inline std::error_code make_error_code(Err e) {
    return std::error_code(static_cast<int>(e), err_category());
}

inline std::error_condition make_error_condition(Err e) {
    return std::error_condition(static_cast<int>(e), err_category());
}

} // namespace cppload

namespace std {
    template <>
    struct is_error_code_enum<cppload::Err> : true_type {};
}
