// @author ssrjkk | cppload
#include "cppload/scenario/engine.hpp"
#include "cppload/metrics/collector.hpp"
#include "cppload/metrics/prometheus_exporter.hpp"
#include "cppload/security/auth_provider.hpp"
#include "cppload/vault/vault_client.hpp"
#include "cppload/otel/exporter.hpp"
#include "cppload/core/constants.hpp"
#include "cppload/core/token_bucket.hpp"
#include "cppload/core/url_parse.hpp"
#include "cppload/core/term.hpp"
#include "cppload/core/redact.hpp"
#include "cppload/net/http_client.hpp"
#include "cppload/net/connection_pool.hpp"
#include <boost/asio/io_context.hpp>
#include <nlohmann/json.hpp>
#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <unordered_map>
#include <thread>
#include <atomic>
#include <mutex>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <cmath>
#include <algorithm>

using namespace cppload;
using json = nlohmann::json;

std::atomic<bool> g_stop_requested{false};

void signal_handler(int) {
    g_stop_requested = true;
}

enum class LogLevel {
    Trace = 0,
    Debug = 1,
    Info  = 2,
    Warn  = 3,
    Error = 4,
};

struct CliArgs {
    std::string config;
    std::string target;
    std::string auth_type;
    std::string auth_token;
    std::string client_id;
    std::string client_secret;
    std::string token_endpoint;
    std::string vault_addr;
    std::string vault_token;
    std::string otlp_endpoint;
    std::string json_path;
    std::string init_preset;
    std::string init_output;
    std::string log_level_str;
    std::string max_body_str;
    int rps{100};
    int duration{60};
    bool duration_set{false};
    bool help{false};
    bool version{false};
    bool verbose{false};
    bool error{false};
    bool dry_run{false};
    bool no_progress{false};
    LogLevel log_level{LogLevel::Info};
    size_t max_body_bytes{core::kDefaultMaxBodyBytes};
};

LogLevel parse_log_level(const std::string& s) {
    std::string low;
    low.resize(s.size());
    std::transform(s.begin(), s.end(), low.begin(),
        [](unsigned char c){ return std::tolower(c); });
    if (low == "trace") return LogLevel::Trace;
    if (low == "debug") return LogLevel::Debug;
    if (low == "info")  return LogLevel::Info;
    if (low == "warn" || low == "warning") return LogLevel::Warn;
    if (low == "err" || low == "error") return LogLevel::Error;
    return LogLevel::Info;
}

size_t parse_size_bytes(const std::string& s) {
    if (s.empty()) return core::kDefaultMaxBodyBytes;
    size_t start = 0;
    while (start < s.size() && std::isspace(static_cast<unsigned char>(s[start]))) ++start;
    size_t end = s.size();
    while (end > start && std::isspace(static_cast<unsigned char>(s[end-1]))) --end;
    std::string v = s.substr(start, end - start);
    if (v.empty()) return core::kDefaultMaxBodyBytes;
    char suffix = 0;
    if (!std::isdigit(static_cast<unsigned char>(v.back()))) {
        suffix = static_cast<char>(std::tolower(static_cast<unsigned char>(v.back())));
        v.pop_back();
    }
    while (!v.empty() && std::isspace(static_cast<unsigned char>(v.back()))) v.pop_back();
    if (v.empty()) return core::kDefaultMaxBodyBytes;
    double base = 0.0;
    try {
        base = std::stod(v);
    } catch (...) {
        return core::kDefaultMaxBodyBytes;
    }
    switch (suffix) {
        case 'k': base *= 1024.0; break;
        case 'm': base *= 1024.0 * 1024.0; break;
        case 'g': base *= 1024.0 * 1024.0 * 1024.0; break;
        case 0: break;
        default: break;
    }
    if (base < 0) return core::kDefaultMaxBodyBytes;
    if (base > static_cast<double>(std::numeric_limits<size_t>::max())) return std::numeric_limits<size_t>::max();
    return static_cast<size_t>(base);
}

CliArgs parse_args(int argc, char* argv[]) {
    CliArgs args;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--help" || arg == "-h") { args.help = true; break; }
        if (arg == "--version" || arg == "-v") { args.version = true; break; }
        auto eq = arg.find('=');
        std::string key = (eq != std::string::npos) ? arg.substr(0, eq) : arg;
        auto next = [&]() -> std::string {
            if (eq != std::string::npos) return arg.substr(eq + 1);
            if (i + 1 < argc) return argv[++i];
            return "";
        };
        if (key == "--config" || key == "-c") args.config = next();
        else if (key == "--target" || key == "-t") args.target = next();
        else if (key == "--rps" || key == "-r") {
            auto s = next();
            try {
                int v = std::stoi(s);
                if (v < 1 || v > 1000000) {
                    std::cerr << core::term::error_label()
                              << "Invalid --rps: " << s
                              << " expected integer in [1, 1000000]\n";
                    args.error = true;
                } else {
                    args.rps = v;
                }
            } catch (const std::exception&) {
                std::cerr << core::term::error_label() << "Invalid --rps: " << s << "\n";
                args.error = true;
            }
        }
        else if (key == "--duration" || key == "-d") {
            auto s = next();
            args.duration_set = true;
            try {
                int v = std::stoi(s);
                if (v < 0 || v > 315360000) {
                    std::cerr << core::term::error_label()
                              << "Invalid --duration: " << s
                              << " expected integer in [0, 315360000]\n";
                    args.error = true;
                } else {
                    args.duration = v;
                }
            } catch (const std::exception&) {
                std::cerr << core::term::error_label() << "Invalid --duration: " << s << "\n";
                args.error = true;
            }
        }
        else if (key == "--auth-type") args.auth_type = next();
        else if (key == "--auth-token") args.auth_token = next();
        else if (key == "--client-id") args.client_id = next();
        else if (key == "--client-secret") args.client_secret = next();
        else if (key == "--token-endpoint") args.token_endpoint = next();
        else if (key == "--vault-addr") args.vault_addr = next();
        else if (key == "--vault-token") args.vault_token = next();
        else if (key == "--otlp-endpoint") args.otlp_endpoint = next();
        else if (key == "--verbose") { args.verbose = true; args.log_level = LogLevel::Debug; }
        else if (key == "--log-level") {
            args.log_level_str = next();
            args.log_level = parse_log_level(args.log_level_str);
        }
        else if (key == "--json") args.json_path = next();
        else if (key == "--dry-run" || key == "--validate-config") args.dry_run = true;
        else if (key == "--no-progress") args.no_progress = true;
        else if (key == "--init") args.init_preset = next();
        else if (key == "--output" || key == "-o") args.init_output = next();
        else if (key == "--max-body") {
            args.max_body_str = next();
            args.max_body_bytes = parse_size_bytes(args.max_body_str);
        }
        else if (key == "--help" || key == "-h") args.help = true;
        else if (key == "--version" || key == "-v") args.version = true;
        else {
            std::cerr << core::term::error_label() << "Unknown option: " << key << "\n";
            args.error = true;
        }
    }
    return args;
}

void print_help(const char* prog) {
    using namespace core::term;
    std::cout
        << wrap("cppload-pro CLI", bright_cyan() + bold()) << "\n"
        << wrap("High-performance HTTP/TCP/WS load tester (C++20)", cyan()) << "\n\n"
        << wrap("Usage:", bold()) << " " << prog << " [options]\n\n"
        << wrap("Required (one of):", bold()) << "\n"
        << "  -c, --config=FILE             " << wrap("YAML scenario path", yellow()) << "\n"
        << "  -t, --target=URL              " << wrap("Direct target (e.g. http://host:8080/path)", yellow()) << "\n\n"
        << wrap("Load configuration:", bold()) << "\n"
        << "  -r, --rps=N                   " << wrap("Target requests per second [1,1000000] (default 100)", yellow()) << "\n"
        << "  -d, --duration=N              " << wrap("Duration seconds [0,315360000]; 0=unlimited (default 60)", yellow()) << "\n"
        << "      --max-body=SIZE[k|m|g]    " << wrap("Cap response body bytes (default 100M)", yellow()) << "\n"
        << "      --no-progress             " << wrap("Disable live progress output", yellow()) << "\n\n"
        << wrap("Authentication:", bold()) << "\n"
        << "      --auth-type=TYPE          " << wrap("none | bearer | apikey | oauth2", yellow()) << "\n"
        << "      --auth-token=TOKEN        " << wrap("Bearer token or API key value", yellow()) << "\n"
        << "      --client-id=ID            " << wrap("OAuth2 client ID", yellow()) << "\n"
        << "      --client-secret=SECRET    " << wrap("OAuth2 client secret (masked in logs)", yellow()) << "\n"
        << "      --token-endpoint=URL      " << wrap("OAuth2 token endpoint URL", yellow()) << "\n\n"
        << wrap("Secrets & Observability:", bold()) << "\n"
        << "      --vault-addr=URL          " << wrap("HashiCorp Vault server address", yellow()) << "\n"
        << "      --vault-token=TOKEN       " << wrap("Vault access token", yellow()) << "\n"
        << "      --otlp-endpoint=URL       " << wrap("OpenTelemetry OTLP traces endpoint", yellow()) << "\n\n"
        << wrap("Output & validation:", bold()) << "\n"
        << "      --json=PATH               " << wrap("Write results as JSON (use '-' for stdout)", yellow()) << "\n"
        << "      --dry-run                 " << wrap("Validate config only, do not send requests", yellow()) << "\n"
        << "      --validate-config         " << wrap("Alias for --dry-run", yellow()) << "\n"
        << "      --log-level=LEVEL         " << wrap("trace|debug|info|warn|error (default info)", yellow()) << "\n"
        << "      --verbose                 " << wrap("Alias for --log-level=debug + per-request output", yellow()) << "\n"
        << "  -h, --help                    " << wrap("Show this help and exit", yellow()) << "\n"
        << "  -v, --version                 " << wrap("Show version and exit", yellow()) << "\n\n"
        << wrap("Scenario templates:", bold()) << "\n"
        << "      --init=PRESET             " << wrap("Generate YAML template: minimal|ecommerce|auth|spike", yellow()) << "\n"
        << "  -o, --output=PATH             " << wrap("Write template to PATH (default cppload-scenario.yaml)", yellow()) << "\n\n"
        << wrap("Exit codes:", bold()) << "\n"
        << "   0  " << wrap("OK", green() + bold()) << " — test ran, SLA passed\n"
        << "   1  " << wrap("USAGE/CONFIG", bright_red() + bold()) << " — invalid CLI args or config schema\n"
        << "   2  " << wrap("SLA FAIL", bright_yellow() + bold()) << " — test completed, SLA thresholds violated\n"
        << "   3  " << wrap("RUNTIME ERR", bright_red() + bold()) << " — worker/runtime failure, test aborted\n\n"
        << wrap("Environment variables:", bold()) << "\n"
        << "  NO_COLOR=1                    " << wrap("Disable ANSI colors (e.g. non-TTY, CI logs)", yellow()) << "\n\n"
        << wrap("Examples:", bold()) << "\n"
        << "  " << prog << " --config=scenarios/ecommerce/load-test.yaml --rps=500 --duration=120\n"
        << "  " << prog << " --target=http://localhost:8080/health --rps=1000 --duration=60 --json=results.json\n"
        << "  " << prog << " --dry-run --config=my-test.yaml\n"
        << "  " << prog << " --init=ecommerce -o load.yaml && " << prog << " --validate-config --config=load.yaml\n"
        << "  TARGET_URL=http://127.0.0.1:8080 " << prog << " --config=scenarios/smoke/load-test.yaml --rps=200 -d 10\n";
}

namespace {

struct InitTemplate { const char* preset; std::string_view contents; };

constexpr std::string_view kTplMinimal = R"(# @generated by cppload-cli --init=minimal
# Minimal working load scenario. See README for field reference.
version: "1.0"                         # Schema version (string, required)
test_id: "minimal-smoke-${USER:-anon}" # Unique test identifier (string, required)

target:
  base_url: ${TARGET_URL:-http://127.0.0.1:8080}  # Target host + base path (required)
  protocol: http1.1                   # Protocol: http1.1 | tcp | websocket
  tls:
    verify: true                      # Verify TLS certificates (bool)

# Load stages. Remove this block to use CLI --rps/--duration as a single default stage.
load_profile:
  - stage: steady                     # Human-readable stage name
    duration: 30s                     # Duration: Ns / Nm / Nh
    target_rps: 100                   # Target requests/second for this stage
    concurrent_users: 10              # Number of parallel worker threads

scenarios:
  - name: "health_check"              # Scenario name for reporting
    weight: 100                        # Selection weight: 0..1000 (higher = picked more often)
    steps:
      - http:
          method: GET                 # HTTP method: GET | POST | PUT | DELETE | PATCH | HEAD | OPTIONS
          path: "/health"             # Request path
          assertions:                 # Optional: fail request unless every assertion passes
            - status_code == 200
            - latency < 500ms

sla:
  error_rate: "< 1%"                  # Max allowed error percentage (0-100)
  p99_latency: "< 1000ms"             # Max allowed P99 response latency
)";

constexpr std::string_view kTplEcommerce = R"(# @generated by cppload-cli --init=ecommerce
version: "1.0"
test_id: "ecommerce-checkout-${BUILD_ID:-local}"

target:
  base_url: ${TARGET_URL:-http://gateway:8080}
  protocol: http1.1
  tls:
    verify: true

authentication:
  type: oauth2
  token_endpoint: ${AUTH_URL:-https://auth.internal/oauth/token}
  client_credentials:
    client_id: ${CLIENT_ID}
    client_secret: ${CLIENT_SECRET}

load_profile:
  - stage: rampup
    duration: 5m
    target_rps: 1000
    concurrent_users: 10
  - stage: steady
    duration: 30m
    target_rps: 5000
    concurrent_users: 50
  - stage: spike
    duration: 2m
    target_rps: 15000
    concurrent_users: 100

scenarios:
  - name: "browse_products"
    weight: 60
    steps:
      - http:
          method: GET
          path: "/api/v1/products"
          assertions: [status_code == 200, latency < 200ms]
      - http:
          method: GET
          path: "/api/v1/products/${PRODUCT_ID:-42}"
          assertions: [status_code == 200, latency < 250ms]
  - name: "user_checkout_flow"
    weight: 40
    steps:
      - http:
          method: POST
          path: "/api/v1/cart"
          body: '{"item_id": "${ITEM_ID:-1001}","qty":1}'
          headers: {Content-Type: "application/json"}
          assertions: [status_code == 201, latency < 300ms]
      - http:
          method: POST
          path: "/api/v1/checkout"
          body: '{"payment_method":"card"}'
          headers: {Content-Type: "application/json"}
          assertions: [status_code == 200, latency < 500ms]

observability:
  metrics:
    prometheus:
      enabled: true
      port: 9095
  tracing:
    otlp_endpoint: ${OTLP_ENDPOINT:-http://jaeger:4318}
    sample_rate: 0.1
  logging:
    level: info
    format: json

sla:
  error_rate: "< 0.1%"
  p99_latency: "< 500ms"
)";

constexpr std::string_view kTplAuth = R"(# @generated by cppload-cli --init=auth
version: "1.0"
test_id: "auth-flow-${CI_BUILD:-dev}"

target:
  base_url: ${TARGET_URL:-http://api.internal}
  tls: { verify: true }

load_profile:
  - stage: steady
    duration: 10m
    target_rps: 500
    concurrent_users: 20

scenarios:
  - name: "login_and_profile"
    weight: 100
    steps:
      - http:
          method: POST
          path: "/oauth/token"
          body: 'grant_type=password&username=${TEST_USER}&password=${TEST_PASS}'
          headers: {Content-Type: "application/x-www-form-urlencoded"}
          assertions: [status_code == 200]
      - http:
          method: GET
          path: "/api/v1/me/profile"
          assertions: [status_code == 200, latency < 150ms]
      - http:
          method: POST
          path: "/api/v1/logout"
          assertions: [status_code == 204]

sla: { error_rate: "< 1%", p99_latency: "< 1000ms" }
)";

constexpr std::string_view kTplSpike = R"(# @generated by cppload-cli --init=spike
version: "1.0"
test_id: "spike-traffic-test"

target:
  base_url: ${TARGET_URL:-http://edge-lb:80}

load_profile:
  - stage: baseline duration=2m target_rps=100 concurrent_users=5
  - stage: spike1   duration=1m target_rps=5000 concurrent_users=200
  - stage: recovery duration=2m target_rps=500 concurrent_users=30
  - stage: spike2   duration=1m target_rps=20000 concurrent_users=500
  - stage: cool     duration=5m target_rps=200 concurrent_users=10

scenarios:
  - name: "spike_get"
    weight: 90
    steps:
      - http:
          method: GET
          path: ${SPIKE_PATH:-/api/v1/items}
          headers: {Accept-Encoding: "gzip,br"}
          assertions: [status_code == 200]
  - name: "spike_post"
    weight: 10
    steps:
      - http:
          method: POST
          path: "/api/v1/events"
          body: '{"ts":0,"evt":"bench"}'
          headers: {Content-Type: "application/json"}
          assertions: [status_code == 202]

sla: { error_rate: "< 5%", p99_latency: "< 5000ms" }
)";

const InitTemplate kTemplates[] = {
    {"minimal",   kTplMinimal},
    {"ecommerce", kTplEcommerce},
    {"auth",      kTplAuth},
    {"spike",     kTplSpike},
};
constexpr size_t kTemplatesCount = sizeof(kTemplates) / sizeof(kTemplates[0]);

} // namespace

int do_init(const CliArgs& args) {
    using namespace core::term;
    std::string preset = args.init_preset;
    std::transform(preset.begin(), preset.end(), preset.begin(),
        [](unsigned char c){ return std::tolower(c); });
    const InitTemplate* found = nullptr;
    for (size_t i = 0; i < kTemplatesCount; ++i) {
        if (preset == kTemplates[i].preset) { found = &kTemplates[i]; break; }
    }
    if (!found) {
        std::cerr << error_label() << "Unknown --init preset: " << args.init_preset << "\n"
                  << info_label() << "Valid presets: ";
        for (size_t i = 0; i < kTemplatesCount; ++i) {
            if (i) std::cerr << ", ";
            std::cerr << kTemplates[i].preset;
        }
        std::cerr << "\n";
        return static_cast<int>(core::ExitCode::ConfigOrUsageError);
    }
    std::string out_path = args.init_output.empty()
        ? std::string("cppload-scenario.yaml") : args.init_output;
    std::ofstream ofs(out_path, std::ios::binary | std::ios::trunc);
    if (!ofs) {
        std::cerr << error_label() << "Cannot write template to: " << out_path << "\n";
        return static_cast<int>(core::ExitCode::ConfigOrUsageError);
    }
    ofs.write(found->contents.data(), static_cast<std::streamsize>(found->contents.size()));
    ofs.close();
    std::cout << ok_label() << "Template '" << found->preset << "' written to: " << out_path << "\n"
              << info_label() << "Validating...\n";
    scenario::ScenarioEngine eng(out_path);
    if (!eng.load_config()) {
        std::cerr << error_label() << "Template config load failed: "
                  << core::redact::safe_log(eng.last_error()) << "\n";
        return static_cast<int>(core::ExitCode::ConfigOrUsageError);
    }
    if (!eng.validate_schema()) {
        std::cerr << fail_label() << "Template schema INVALID: "
                  << core::redact::safe_log(eng.last_error()) << "\n";
        return static_cast<int>(core::ExitCode::ConfigOrUsageError);
    }
    std::cout << ok_label() << "Template validated OK (test_id=" << eng.config().test_id
              << ", stages=" << eng.config().load_profile.stages.size()
              << ", scenarios=" << eng.config().scenarios.size() << ")\n"
              << info_label() << "Run it with: TARGET_URL=http://your-host cppload-cli --config="
              << out_path << " --rps=500 --duration=60\n";
    return static_cast<int>(core::ExitCode::OK);
}

security::AuthConfig build_auth_config(const CliArgs& args,
                                       const scenario::ScenarioConfig* cfg) {
    security::AuthConfig auth_cfg;
    if (!args.auth_type.empty()) {
        if (args.auth_type == "bearer" && !args.auth_token.empty()) {
            auth_cfg.type = security::AuthType::BEARER_TOKEN;
            auth_cfg.token = args.auth_token;
        } else if (args.auth_type == "apikey" && !args.auth_token.empty()) {
            auth_cfg.type = security::AuthType::API_KEY;
            auth_cfg.api_key = args.auth_token;
        } else if (args.auth_type == "oauth2") {
            auth_cfg.type = security::AuthType::OAUTH2;
            auth_cfg.client_id = args.client_id;
            auth_cfg.client_secret = args.client_secret;
            auth_cfg.token_endpoint = args.token_endpoint;
        }
    } else if (cfg && cfg->authentication.type == "oauth2") {
        auth_cfg.type = security::AuthType::OAUTH2;
        auth_cfg.client_id = cfg->authentication.client_credentials.client_id;
        auth_cfg.client_secret = cfg->authentication.client_credentials.client_secret;
        auth_cfg.token_endpoint = cfg->authentication.token_endpoint;
    }
    return auth_cfg;
}

int do_dry_run(const std::string& config_path) {
    using namespace core::term;
    scenario::ScenarioEngine engine(config_path);
    if (!engine.load_config()) {
        std::cerr << fail_label() << "Config load failed: "
                  << core::redact::safe_log(engine.last_error()) << "\n";
        return static_cast<int>(core::ExitCode::ConfigOrUsageError);
    }
    if (!engine.validate()) {
        std::cerr << fail_label() << "Config invalid: "
                  << core::redact::safe_log(engine.last_error()) << "\n";
        return static_cast<int>(core::ExitCode::ConfigOrUsageError);
    }
    if (!engine.validate_schema()) {
        std::cerr << fail_label() << "Schema: "
                  << core::redact::safe_log(engine.last_error()) << "\n";
        return static_cast<int>(core::ExitCode::ConfigOrUsageError);
    }
    const auto& cfg = engine.config();
    std::cout << ok_label() << "Config OK\n"
              << info_label() << "test_id:     " << cfg.test_id << "\n"
              << info_label() << "target:      " << cfg.target.base_url << "\n"
              << info_label() << "stages:      " << cfg.load_profile.stages.size() << "\n"
              << info_label() << "scenarios:   " << cfg.scenarios.size() << "\n";
    for (size_t i = 0; i < cfg.load_profile.stages.size(); ++i) {
        const auto& s = cfg.load_profile.stages[i];
        std::cout << info_label() << "  stage[" << i << "] name=" << s.name
                  << " duration=" << s.duration.count() << "ms"
                  << " rps=" << s.target_rps
                  << " users=" << s.concurrent_users << "\n";
    }
    return static_cast<int>(core::ExitCode::OK);
}

json snapshot_to_json(const metrics::RequestMetrics& m) {
    return json{
        {"total_requests",         m.total_requests},
        {"successful_requests",    m.successful_requests},
        {"failed_requests",        m.failed_requests},
        {"total_bytes_sent",       m.total_bytes_sent},
        {"total_bytes_received",   m.total_bytes_received},
        {"mean_latency_us",        m.mean_latency_us},
        {"min_latency_us",         static_cast<uint64_t>(m.min_latency.count())},
        {"max_latency_us",         static_cast<uint64_t>(m.max_latency.count())},
        {"p95_latency_us",         m.p95_latency_us},
        {"p99_latency_us",         m.p99_latency_us},
    };
}

int print_results(const metrics::MetricsCollector& metrics,
                  std::chrono::seconds elapsed,
                  const std::string& test_id,
                  bool sla_passed_in,
const CliArgs& args,
                   std::chrono::system_clock::time_point started_at) {
    using namespace core::term;
    auto m = metrics.snapshot();
    double actual_rps = metrics.requests_per_second();
    double err_rate = metrics.error_rate();
    auto end_color = [&]() -> const std::string& {
        return sla_passed_in ? (const std::string&)green() : (const std::string&)bright_red();
    };
    auto head = wrap("cppload-pro — Load Results", bright_cyan() + bold());
    std::cout << "\n" << head << "\n";
    std::cout << std::string(58, '-') << "\n";
    auto line = [&](const std::string& k, const std::string& v, bool ok_color = true) {
        std::cout << "  " << wrap(k + ":", bold())
                  << std::string(std::max(1, 20 - static_cast<int>(k.size())), ' ')
                  << (ok_color ? wrap(v, end_color()) : v) << "\n";
    };
    line("Test ID",       test_id, false);
    line("Elapsed",       std::to_string(elapsed.count()) + " s", false);
    line("Total requests",std::to_string(m.total_requests));
    line("Successful",    std::to_string(m.successful_requests));
    line("Failed",        std::to_string(m.failed_requests));
    std::ostringstream er; er.precision(3); er << err_rate << "%";
    line("Error rate",    er.str());
    std::ostringstream rps; rps.precision(1); rps << actual_rps << " req/s";
    line("Actual RPS",    rps.str(), false);
    std::ostringstream mn; mn << static_cast<uint64_t>(m.mean_latency_us) << " us";
    line("Mean latency",  mn.str(), false);
    line("P50 latency",   std::to_string(metrics.percentile(0.50)) + " us", false);
    line("P95 latency",   std::to_string(m.p95_latency_us) + " us", false);
    line("P99 latency",   std::to_string(m.p99_latency_us) + " us");
    std::cout << std::string(58, '-') << "\n";
    std::cout << "  " << wrap("SLA:", bold())
              << std::string(21, ' ')
              << (sla_passed_in ? sla_pass_label() : sla_fail_label()) << "\n\n";

    if (!args.json_path.empty()) {
        json j = json{
            {"version",     std::string(core::kVersion)},
            {"tool",        "cppload-cli"},
            {"test_id",     test_id},
            {"elapsed_s",   elapsed.count()},
            {"started_at",  std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(
                                started_at.time_since_epoch()).count())},
            {"target_rps",  static_cast<uint64_t>(args.rps)},
            {"duration_s",  args.duration},
            {"sla_passed",  sla_passed_in},
            {"metrics",     snapshot_to_json(m)},
            {"actual_rps",  actual_rps},
            {"error_rate_pct", err_rate},
        };
        std::string body = j.dump(2);
        if (args.json_path == "-") {
            std::cout << wrap("JSON results (stdout):", bright_cyan() + bold()) << "\n"
                      << body << "\n";
        } else {
            std::ofstream ofs(args.json_path, std::ios::binary | std::ios::trunc);
            if (ofs) {
                ofs << body << "\n";
                ofs.close();
                std::cout << ok_label() << "JSON results written to: " << args.json_path << "\n";
            } else {
                std::cerr << error_label() << "Cannot write JSON to: " << args.json_path << "\n";
            }
        }
    }
    return 0;
}

int run_direct(const CliArgs& args, security::AuthProvider& auth, otel::Tracer& tracer) {
    using namespace core::term;
    if (args.rps <= 0) {
        std::cerr << error_label() << "Invalid --rps: must be > 0 (got " << args.rps << ")\n";
        return static_cast<int>(core::ExitCode::ConfigOrUsageError);
    }

    auto u = core::parse_url(args.target);
    if (u.host.empty()) {
        std::cerr << error_label() << "Invalid target URL: cannot parse host from \""
                  << core::redact::safe_log(args.target) << "\"\n";
        return static_cast<int>(core::ExitCode::ConfigOrUsageError);
    }
    uint16_t target_port = 0;
    {
        long parsed_port = 0;
        try {
            parsed_port = u.port.empty()
                ? (u.tls ? 443L : 80L)
                : std::stol(u.port);
        } catch (const std::exception&) {
            std::cerr << error_label() << "Invalid port in target URL: \"" << u.port << "\"\n";
            return static_cast<int>(core::ExitCode::ConfigOrUsageError);
        }
        if (parsed_port <= 0 || parsed_port > 65535) {
            std::cerr << error_label() << "Port out of range in target URL: " << parsed_port << "\n";
            return static_cast<int>(core::ExitCode::ConfigOrUsageError);
        }
        target_port = static_cast<uint16_t>(parsed_port);
    }

    uint32_t concurrency = static_cast<uint32_t>(std::min<uint64_t>(
        args.rps / 10, 64));
    if (concurrency == 0) concurrency = 1;
    if (concurrency > 100000u) concurrency = 100000u;

    std::cout << wrap("cppload-pro Load Tester (direct mode)", bright_cyan() + bold()) << "\n"
              << info_label() << "Target:      " << args.target << "\n"
              << info_label() << "RPS:         " << args.rps << "\n"
              << info_label() << "Concurrency: " << concurrency << "\n"
              << info_label() << "Duration:    " << args.duration << "s\n"
              << std::string(58, '-') << "\n";

    auto test_start = std::chrono::steady_clock::now();
    auto test_start_wall = std::chrono::system_clock::now();
    auto end = args.duration > 0
        ? test_start + std::chrono::seconds(args.duration)
        : std::chrono::steady_clock::time_point::max();

    metrics::MetricsCollector metrics;
    TokenBucket bucket(static_cast<double>(args.rps), std::max(100.0, static_cast<double>(args.rps)));

    tracer.start_span("load_test");
    tracer.add_attribute("target", args.target);
    tracer.add_attribute("target_rps", std::to_string(args.rps));
    tracer.add_attribute("duration_s", std::to_string(args.duration));
    tracer.add_attribute("concurrency", std::to_string(concurrency));

    std::mutex cout_mtx;
    std::atomic<uint32_t> worker_failures{0};
    std::string stage_name = "direct";

    std::atomic<bool> show_progress{!args.no_progress && core::term::stdout_is_tty()};
    std::thread watcher([&]() {
        auto last = std::chrono::steady_clock::now();
        auto interval = show_progress.load()
            ? std::chrono::milliseconds(250)
            : std::chrono::milliseconds(5000);
        while (!g_stop_requested && std::chrono::steady_clock::now() < end) {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            auto now = std::chrono::steady_clock::now();
            if (now - last >= interval) {
                last = now;
                auto snap = metrics.snapshot();
                double rps = metrics.requests_per_second();
                double err = metrics.error_rate();
                auto el = std::chrono::duration_cast<std::chrono::seconds>(now - test_start);
                std::ostringstream line;
                line << "[" << stage_name << " elapsed=" << el.count() << "s"
                     << " RPS=" << std::fixed << std::setprecision(1) << rps
                     << " err=" << std::fixed << std::setprecision(3) << err << "%"
                     << " p99=" << snap.p99_latency_us << "us]";
                std::lock_guard<std::mutex> lk(cout_mtx);
                if (show_progress.load()) {
                    std::cout << "\r\033[K" << wrap(line.str(), blue());
                    std::cout.flush();
                } else {
                    std::cout << info_label() << line.str() << "\n";
                }
            }
        }
        if (show_progress.load()) std::cout << "\n";
    });

    std::vector<std::thread> workers;
    workers.reserve(concurrency);
    for (uint32_t w = 0; w < concurrency; ++w) {
        workers.emplace_back([&, w]() {
            try {
                boost::asio::io_context ioc;
                auto guard = boost::asio::make_work_guard(ioc);
                net::Http11Client client(ioc);
                client.set_timeout(core::kDefaultTimeout);
                client.set_max_body_bytes(args.max_body_bytes);

                double rate = static_cast<double>(args.rps);
                if (rate <= 0) rate = 1.0;
                double period_ns_d = concurrency / rate * 1'000'000'000.0;
                if (period_ns_d < 1.0) period_ns_d = 1.0;
                if (period_ns_d > 1e18) period_ns_d = 1e18;
                auto slot_period = std::chrono::nanoseconds(static_cast<int64_t>(period_ns_d));
                auto next_slot = std::chrono::steady_clock::now() +
                    slot_period * static_cast<int64_t>(w) /
                    static_cast<int64_t>(concurrency);
                bool first_slot = true;

                while (!g_stop_requested) {
                    auto now = std::chrono::steady_clock::now();
                    if (now >= end) break;
                    if (!first_slot) {
                        next_slot += slot_period;
                        if (now < next_slot) {
                            auto sleep_dur = next_slot - now;
                            if (sleep_dur > std::chrono::milliseconds(2)) {
                                auto wake_at = next_slot - std::chrono::milliseconds(2);
                                while (std::chrono::steady_clock::now() < wake_at && !g_stop_requested) {
                                    auto rem = wake_at - std::chrono::steady_clock::now();
                                    std::this_thread::sleep_for(std::min<std::chrono::steady_clock::duration>(
                                        rem, std::chrono::milliseconds(10)));
                                }
                            }
                            while (std::chrono::steady_clock::now() < next_slot && !g_stop_requested) {
                                // precise spin
                            }
                            now = std::chrono::steady_clock::now();
                            if (now >= end) break;
                        }
                    } else {
                        first_slot = false;
                    }
                    if (!bucket.try_consume()) continue;

                    net::Request req;
                    req.method = "GET";
                    req.path = u.path;
                    req.host = u.host;
                    req.port = target_port;
                    req.use_tls = u.tls;
                    auth.apply_headers(req.headers);

                    auto capture_req = std::make_shared<net::Request>(std::move(req));
                    auto done = std::make_shared<std::atomic<bool>>(false);
                    client.async_request(*capture_req,
                        [&, capture_req, done](std::error_code ec, net::Response resp) {
                            uint16_t code = ec ? 0 : resp.status_code;
                            metrics.record_request(code, resp.latency,
                                                   capture_req->body.size(), resp.body.size());
                            if (args.verbose && args.log_level <= LogLevel::Debug) {
                                std::lock_guard<std::mutex> lock(cout_mtx);
                                if (ec) {
                                    std::cout << core::term::error_label()
                                              << core::redact::safe_log(ec.message()) << "\n";
                                } else {
                                    std::ostringstream ss;
                                    ss << code << " " << resp.latency.count() << "us";
                                    std::cout << wrap(ss.str(), code >= 400 ? red() : cyan()) << "\n";
                                }
                            }
                            tracer.start_span("GET " + capture_req->path);
                            tracer.add_attribute("http.method", "GET");
                            tracer.add_attribute("http.url", capture_req->path);
                            tracer.add_attribute("http.status_code", std::to_string(resp.status_code));
                            tracer.add_attribute("http.latency_us", std::to_string(resp.latency.count()));
                            tracer.end_span();
                            done->store(true, std::memory_order_release);
                        });
                    while (!done->load(std::memory_order_acquire) && !g_stop_requested) {
                        ioc.run_one();
                    }
                }
                guard.reset();
                ioc.restart();
                ioc.run();
            } catch (const std::exception& e) {
                std::lock_guard<std::mutex> lk(cout_mtx);
                std::cerr << error_label() << "direct worker error: "
                          << core::redact::safe_log(e.what()) << "\n";
                worker_failures.fetch_add(1, std::memory_order_relaxed);
            }
        });
    }

    for (auto& t : workers) if (t.joinable()) t.join();
    g_stop_requested = true;
    if (watcher.joinable()) watcher.join();
    tracer.end_span();

    bool runtime_failed = worker_failures.load(std::memory_order_relaxed) > 0;
    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::steady_clock::now() - test_start);

    // SLA for direct mode: lenient defaults — >10% error rate or p99>5s is FAIL.
    bool sla_passed = metrics.error_rate() <= 10.0 &&
                      metrics.snapshot().p99_latency_us <= 5 * 1000ULL * 1000ULL;

    print_results(metrics, elapsed, "direct", sla_passed, args, test_start_wall);

    if (runtime_failed) return static_cast<int>(core::ExitCode::RuntimeError);
    return sla_passed ? static_cast<int>(core::ExitCode::OK)
                      : static_cast<int>(core::ExitCode::SlaFailed);
}

int main(int argc, char* argv[]) {
    CliArgs args = parse_args(argc, argv);

    if (args.error) {
        std::cerr << core::term::info_label()
                  << "Run '" << argv[0] << " --help' for usage.\n";
        return static_cast<int>(core::ExitCode::ConfigOrUsageError);
    }
    if (args.help) { print_help(argv[0]); return static_cast<int>(core::ExitCode::OK); }
    if (args.version) {
        std::cout << core::term::wrap(
            "cppload-pro " + std::string(core::kVersion),
            core::term::bright_cyan() + core::term::bold()) << "\n";
        return static_cast<int>(core::ExitCode::OK);
    }
    if (!args.init_preset.empty()) {
        return do_init(args);
    }
    if (args.dry_run) {
        if (args.config.empty()) {
            std::cerr << core::term::error_label()
                      << "--dry-run requires --config=FILE\n";
            return static_cast<int>(core::ExitCode::ConfigOrUsageError);
        }
        return do_dry_run(args.config);
    }

    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    std::unique_ptr<scenario::ScenarioEngine> engine;

    if (!args.config.empty()) {
        engine = std::make_unique<scenario::ScenarioEngine>(args.config);
        if (!engine->load_config()) {
            std::cerr << core::term::fail_label() << "Config error: "
                      << core::redact::safe_log(engine->last_error()) << "\n";
            return static_cast<int>(core::ExitCode::ConfigOrUsageError);
        }
        if (!engine->validate() || !engine->validate_schema()) {
            std::cerr << core::term::fail_label() << "Validation error: "
                      << core::redact::safe_log(engine->last_error()) << "\n";
            return static_cast<int>(core::ExitCode::ConfigOrUsageError);
        }
    } else if (args.target.empty()) {
        std::cerr << core::term::error_label()
                  << "Either --config or --target is required\n\n";
        print_help(argv[0]);
        return static_cast<int>(core::ExitCode::ConfigOrUsageError);
    }

    security::AuthConfig auth_cfg = build_auth_config(
        args, engine ? &engine->config() : nullptr);
    std::shared_ptr<security::AuthProvider> auth;
    if (auth_cfg.type != security::AuthType::NONE) {
        try {
            auth = std::make_shared<security::AuthProvider>(auth_cfg);
        } catch (const std::exception& e) {
            std::cerr << core::term::error_label() << "Auth setup failed: "
                      << core::redact::safe_log(e.what()) << "\n";
            return static_cast<int>(core::ExitCode::ConfigOrUsageError);
        }
    }

    std::unique_ptr<vault::VaultClient> vault;
    if (!args.vault_addr.empty()) {
        vault::VaultConfig vc;
        vc.address = args.vault_addr;
        vc.token = args.vault_token;
        vault = std::make_unique<vault::VaultClient>(vc);
        if (vault->is_connected()) {
            std::cout << core::term::ok_label() << "Vault connected: " << vc.address << "\n";
        } else {
            std::cout << core::term::warn_label() << "Vault not available\n";
        }
    }

    otel::TraceConfig tc;
    if (!args.otlp_endpoint.empty()) tc.endpoint = args.otlp_endpoint;
    otel::Tracer tracer(tc);

    if (!engine) {
        security::AuthProvider noop;
        return run_direct(args, auth ? *auth : noop, tracer);
    }

    if (args.rps > 0) {
        engine->set_target_rps(static_cast<uint32_t>(args.rps));
    }
    if (args.duration_set && args.duration > 0) {
        engine->set_max_duration(std::chrono::seconds(args.duration));
    }
    if (auth) {
        engine->set_auth_provider(auth);
    }

    auto test_start_wall = std::chrono::system_clock::now();
    auto test_start = std::chrono::steady_clock::now();

    std::cout << core::term::wrap("cppload-pro Load Tester (scenario mode)",
                                  core::term::bright_cyan() + core::term::bold()) << "\n"
              << core::term::info_label() << "Test ID:  " << engine->config().test_id << "\n"
              << core::term::info_label() << "Target:   " << engine->config().target.base_url << "\n"
              << core::term::info_label() << "CLI RPS:  " << args.rps << "\n"
              << core::term::info_label() << "Duration: " << args.duration << "s\n"
              << std::string(58, '-') << "\n";
    if (!engine->config().target.tls.verify) {
        std::cout << core::term::warn_label()
                  << "TLS certificate verification is DISABLED (target.tls.verify=false)\n";
    }

    tracer.start_span("load_test");
    tracer.add_attribute("test_id", engine->config().test_id);
    tracer.add_attribute("target", engine->config().target.base_url);
    tracer.add_attribute("target_rps", std::to_string(args.rps));
    tracer.add_attribute("duration_s", std::to_string(args.duration));

    metrics::MetricsCollector metrics;

    metrics::PrometheusExporter prometheus(
        "127.0.0.1:" + std::to_string(engine->config().observability.metrics.prometheus.port));
    if (engine->config().observability.metrics.prometheus.enabled) {
        if (prometheus.start()) {
            std::cout << core::term::ok_label()
                      << "Prometheus exporter: " << prometheus.endpoint() << "\n";
        } else {
            std::cout << core::term::warn_label()
                      << "Prometheus exporter: failed to start\n";
        }
    }

    std::mutex cout_mtx;
    const auto& stages = engine->config().load_profile.stages;

    std::thread watcher([&]() {
        auto last_update = std::chrono::steady_clock::now();
        auto last_progress = last_update;
        bool show_live = !args.no_progress && core::term::stdout_is_tty();
        auto interval_prom = std::chrono::seconds(1);
        auto interval_progress = show_live
            ? std::chrono::milliseconds(250)
            : std::chrono::milliseconds(5000);
        auto started = std::chrono::steady_clock::now();
        while (!g_stop_requested) {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            auto now = std::chrono::steady_clock::now();
            if (prometheus.is_running() && now - last_update >= interval_prom) {
                prometheus.update_metrics(metrics);
                last_update = now;
            }
            if (now - last_progress >= interval_progress) {
                last_progress = now;
                auto snap = metrics.snapshot();
                double rps = metrics.requests_per_second();
                double err = metrics.error_rate();
                auto el = std::chrono::duration_cast<std::chrono::seconds>(now - started);
                size_t stage_idx = 0;
                size_t total = stages.empty() ? 1 : stages.size();
                std::string stage_name = stages.empty() ? "default" : stages[0].name;
                if (!stages.empty()) {
                    uint64_t acc = 0;
                    for (size_t i = 0; i < stages.size(); ++i) {
                        acc += static_cast<uint64_t>(std::max<int64_t>(1, stages[i].duration.count()));
                        auto total_so_far_ms = el.count() * 1000ULL;
                        if (total_so_far_ms < acc) {
                            stage_idx = i;
                            stage_name = stages[i].name;
                            break;
                        }
                        stage_idx = stages.size() - 1;
                        stage_name = stages.back().name;
                    }
                }
                std::ostringstream line;
                line << "[" << stage_name << " " << (stage_idx+1) << "/" << total
                     << " elapsed=" << el.count() << "s"
                     << " RPS=" << std::fixed << std::setprecision(1) << rps
                     << " err=" << std::fixed << std::setprecision(3) << err << "%"
                     << " p99=" << snap.p99_latency_us << "us]";
                std::lock_guard<std::mutex> lk(cout_mtx);
                if (show_live) {
                    std::cout << "\r\033[K" << core::term::wrap(line.str(), core::term::blue());
                    std::cout.flush();
                } else {
                    std::cout << core::term::info_label() << line.str() << "\n";
                }
            }
        }
        if (show_live) std::cout << "\n";
        engine->stop();
    });

    try {
        engine->run([&](const scenario::HttpStep& step,
                         const net::Response& resp,
                         metrics::MetricsCollector& /* m */) {
            uint16_t code = resp.status_code;
            if (code != 0 && !scenario::evaluate_assertions(step, resp)) {
                code = 0;
            }
            metrics.record_request(code, resp.latency,
                                   step.body.size(), resp.body.size());
            tracer.start_span(step.method + " " + step.path);
            tracer.add_attribute("http.method", step.method);
            tracer.add_attribute("http.url", step.path);
            tracer.add_attribute("http.status_code", std::to_string(resp.status_code));
            tracer.add_attribute("http.latency_us", std::to_string(resp.latency.count()));
            tracer.end_span();
            if (args.verbose && args.log_level <= LogLevel::Debug) {
                std::lock_guard<std::mutex> lk(cout_mtx);
                std::ostringstream ss;
                ss << step.method << " " << step.path << " -> " << resp.status_code
                   << " " << resp.latency.count() << "us";
                std::cout << core::term::wrap(
                    ss.str(), resp.status_code >= 400 ? core::term::red() : core::term::cyan()) << "\n";
            }
        });
    } catch (const std::exception& e) {
        std::cerr << core::term::error_label() << "Load test failed: "
                  << core::redact::safe_log(e.what()) << "\n";
        g_stop_requested = true;
        if (watcher.joinable()) watcher.join();
        tracer.end_span();
        return static_cast<int>(core::ExitCode::RuntimeError);
    }

    g_stop_requested = true;
    if (watcher.joinable()) watcher.join();
    tracer.end_span();

    if (prometheus.is_running()) {
        prometheus.update_metrics(metrics);
        prometheus.stop();
    }

    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::steady_clock::now() - test_start);

    bool run_aborted = engine->run_failed();
    if (run_aborted) {
        std::cerr << core::term::error_label()
                  << "Load test aborted: worker thread failed: "
                  << core::redact::safe_log(engine->last_error()) << "\n";
    }

    bool sla_ok = engine->check_sla(metrics) && !run_aborted;
    print_results(metrics, elapsed, engine->config().test_id, sla_ok, args, test_start_wall);

    if (run_aborted) return static_cast<int>(core::ExitCode::RuntimeError);
    return sla_ok ? static_cast<int>(core::ExitCode::OK)
                  : static_cast<int>(core::ExitCode::SlaFailed);
}
