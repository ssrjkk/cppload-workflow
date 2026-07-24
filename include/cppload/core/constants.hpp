// @author ssrjkk | cppload
#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace cppload::core {

constexpr int kHttpVersion = 11;

constexpr auto kDefaultTimeout = std::chrono::milliseconds{5000};

constexpr std::string_view kUserAgent = "cppload-pro/1.0";
constexpr std::string_view kVersion = "1.0.0";

constexpr size_t kOtlpBatchSize = 64;
constexpr size_t kOtlpMaxBufferedSpans = 4096;

constexpr int kSpanKindClient = 2;
constexpr int kOtlpStatusCodeOk = 1;

constexpr uint32_t kDefaultAuthTimeoutSec = 10;
constexpr int kDefaultTokenExpirySec = 3600;
constexpr int kExpiryMarginSec = 60;

constexpr uint32_t kDefaultConcurrency = 10;
constexpr double kDefaultErrorRate = 0.1;
constexpr auto kDefaultLatencyMs = std::chrono::milliseconds{500};

constexpr size_t kMinShards = 4;

} // namespace cppload::core
