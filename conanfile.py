from conan import ConanFile
from conan.tools.cmake import CMakeToolchain, CMake, cmake_layout

class CPPLoadProConan(ConanFile):
    name = "cppload-pro"
    version = open("VERSION").read().strip()
    settings = "os", "compiler", "build_type", "arch"
    generators = "CMakeDeps"

    def requirements(self):
        self.requires("boost/1.83.0")
        self.requires("openssl/3.2.0")
        self.requires("nlohmann_json/3.11.2")
        self.requires("prometheus-cpp/1.2.4")
        self.requires("grpc/1.63.0")
        self.requires("protobuf/4.25.3")
        self.requires("civetweb/1.16")

    def layout(self):
        cmake_layout(self)

    def generate(self):
        tc = CMakeToolchain(self)
        tc.generate()

    def build(self):
        cmake = CMake(self)
        cmake.configure()
        cmake.build()
