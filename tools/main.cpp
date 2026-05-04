#include <iostream>
#include <string>
#include <boost/program_options.hpp>

namespace po = boost::program_options;

int main(int argc, char* argv[]) {
    po::options_description desc("cppload-pro CLI");
    desc.add_options()
        ("help,h", "Show help")
        ("version,v", "Show version")
        ("config,c", po::value<std::string>(), "Config file path")
        ("target,t", po::value<std::string>(), "Target URL")
        ("rps,r", po::value<int>()->default_value(100), "Requests per second")
        ("duration,d", po::value<int>()->default_value(60), "Duration in seconds");
    
    po::variables_map vm;
    try {
        po::store(po::parse_command_line(argc, argv, desc), vm);
        po::notify(vm);
    } catch (const po::error& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }
    
    if (vm.count("help")) {
        std::cout << desc << "\n";
        return 0;
    }
    
    if (vm.count("version")) {
        std::cout << "cppload-pro 1.0.0\n";
        return 0;
    }
    
    std::cout << "cppload-pro Load Tester\n";
    if (vm.count("target")) {
        std::cout << "Target: " << vm["target"].as<std::string>() << "\n";
    }
    std::cout << "RPS: " << vm["rps"].as<int>() << "\n";
    std::cout << "Duration: " << vm["duration"].as<int>() << "s\n";
    
    return 0;
}
