// @author ssrjkk | volley
#include "cppload/platform/http_server.hpp"
#include "cppload/platform/repository.hpp"

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>

namespace {

struct Config {
    std::string address = "0.0.0.0";
    uint16_t port = 8080;
    bool help = false;
};

auto parse_args(int argc, char* argv[]) -> Config {
    auto cfg = Config{};
    for (int i = 1; i < argc; ++i) {
        auto arg = std::string(argv[i]);
        if (arg == "--help" || arg == "-h") {
            cfg.help = true;
        } else if (arg.rfind("--port=", 0) == 0) {
            cfg.port = static_cast<uint16_t>(std::stoi(arg.substr(7)));
        } else if (arg.rfind("--address=", 0) == 0) {
            cfg.address = arg.substr(10);
        }
    }
    return cfg;
}

void print_help() {
    std::cout << "volley-control-plane — REST API for load test management\n\n"
              << "Usage: volley-control-plane [options]\n\n"
              << "Options:\n"
              << "  --address=ADDR  Bind address (default: 0.0.0.0)\n"
              << "  --port=PORT     Bind port (default: 8080)\n"
              << "  -h, --help      Show this help\n\n"
              << "API endpoints:\n"
              << "  GET    /health                       Health check\n"
              << "  GET    /api/v1/projects              List projects\n"
              << "  POST   /api/v1/projects              Create project\n"
              << "  GET    /api/v1/projects/:id          Get project\n"
              << "  DELETE /api/v1/projects/:id          Delete project\n"
              << "  GET    /api/v1/projects/:id/scenarios  List scenarios\n"
              << "  POST   /api/v1/projects/:id/scenarios  Create scenario\n"
              << "  GET    /api/v1/projects/:id/runs     List runs\n"
              << "  POST   /api/v1/projects/:id/runs     Create run\n"
              << "  GET    /api/v1/projects/:id/policies List policies\n"
              << "  POST   /api/v1/projects/:id/policies Create policy\n"
              << "  POST   /api/v1/evaluate              Evaluate policy\n";
}

} // namespace

int main(int argc, char* argv[]) {
    auto cfg = parse_args(argc, argv);

    if (cfg.help) {
        print_help();
        return 0;
    }

    auto repo = cppload::platform::make_in_memory_repository();
    auto evaluator = std::make_shared<cppload::platform::PolicyEvaluator>(repo);
    auto server = cppload::platform::HttpServer(repo, evaluator, cfg.address, cfg.port);

    std::cout << "volley control plane listening on " << cfg.address << ":" << cfg.port << "\n";
    server.start();

    std::cin.get();
    std::cout << "\nshutting down...\n";
    server.stop();

    return 0;
}
