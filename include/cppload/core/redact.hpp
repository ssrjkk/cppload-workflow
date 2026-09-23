// @author ssrjkk | volley
#pragma once

#include <algorithm>
#include <array>
#include <cctype>
#include <string>
#include <string_view>

namespace cppload::core::redact {

namespace detail {

inline bool iequals(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(a[i])) !=
            std::tolower(static_cast<unsigned char>(b[i]))) {
            return false;
        }
    }
    return true;
}

constexpr std::string_view kMask = "******";

} // namespace detail

// Returns a copy of `input` with known-secret values masked to ******.
// Supported patterns:
//   HTTP header lines like "Authorization: Bearer VALUE\r\n" or "X-API-Key: VAL"
//   JSON key-value like "client_secret":"VAL" / "access_token": "VAL" / "token":"VAL"
//   URL form encoded like client_secret=VAL&access_token=VAL
inline std::string redact_secrets(std::string_view input) {
    static constexpr std::array<std::string_view, 14> kSecretKeys = {
        "authorization",
        "x-api-key",
        "x-vault-token",
        "client_secret",
        "client-id",
        "client_id",
        "access_token",
        "id_token",
        "refresh_token",
        "token",
        "vault_token",
        "password",
        "secret",
        "private_key",
    };

    std::string out(input);
    if (out.empty()) return out;

    auto is_separator = [](char c) -> bool {
        return c == ':' || c == '=' || c == ' ' || c == '\t' || c == '\r' || c == '\n' ||
               c == '"' || c == '\'' || c == '&' || c == ',';
    };

    auto find_key_end = [&](size_t start) -> size_t {
        size_t i = start;
        while (i < out.size() && (std::isalnum(static_cast<unsigned char>(out[i])) || out[i] == '_' || out[i] == '-')) {
            ++i;
        }
        return i;
    };

    for (size_t pos = 0; pos < out.size(); ++pos) {
        size_t key_start = pos;
        size_t key_end = find_key_end(pos);
        if (key_end == key_start) {
            continue;
        }
        std::string_view key_sv(&out[key_start], key_end - key_start);
        bool matched = false;
        for (auto k : kSecretKeys) {
            if (detail::iequals(key_sv, k)) {
                matched = true;
                break;
            }
        }
        if (!matched) {
            pos = key_end;
            continue;
        }
        size_t p = key_end;
        while (p < out.size() && is_separator(out[p]) && out[p] != '\r' && out[p] != '\n') {
            ++p;
        }
        if (p >= out.size()) break;
        char quote = 0;
        if (out[p] == '"' || out[p] == '\'') {
            quote = out[p];
            ++p;
        }
        size_t value_start = p;
        size_t value_end = p;
        while (value_end < out.size()) {
            char c = out[value_end];
            if (quote) {
                if (c == quote) break;
            } else {
                if (c == '\r' || c == '\n' || c == '&' || c == '}' || c == ']' || c == ',') {
                    break;
                }
            }
            ++value_end;
        }
        if (value_end > value_start) {
            out.replace(value_start, value_end - value_start, detail::kMask);
            pos = value_start + detail::kMask.size();
            continue;
        }
        pos = value_end;
    }

    return out;
}

// Wrapper that returns a masked copy of a string; used in verbose logging paths.
inline std::string safe_log(std::string_view msg) {
    return redact_secrets(msg);
}

} // namespace cppload::core::redact
