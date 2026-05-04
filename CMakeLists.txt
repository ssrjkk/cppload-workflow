cmake_minimum_required(VERSION 3.20)
project(cppload-pro VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(GNUInstallDirs)
include(CMakeDependentOption)

# Build options
option(BUILD_SHARED_LIBS "Build shared libraries" OFF)
cmake_dependent_option(CPLOAD_BUILD_TESTS "Build tests" ON "CMAKE_PROJECT_NAME:STREQUAL:PROJECT_NAME" OFF)
cmake_dependent_option(CPLOAD_BUILD_PYTHON "Build Python bindings" ON "CMAKE_PROJECT_NAME:STREQUAL:PROJECT_NAME" OFF)
cmake_dependent_option(CPLOAD_BUILD_WORKERS "Build worker executables" ON "CMAKE_PROJECT_NAME:STREQUAL:PROJECT_NAME" OFF)
cmake_dependent_option(CPLOAD_BUILD_TOOLS "Build CLI tools" ON "CMAKE_PROJECT_NAME:STREQUAL:PROJECT_NAME" OFF)

# Dependencies via Conan or find_package
find_package(Boost 1.83 REQUIRED COMPONENTS system)
find_package(OpenSSL 3.0 REQUIRED)
find_package(Threads REQUIRED)

# Optional dependencies
find_package(nlohmann_json QUIET)
find_package(prometheus-cpp QUIET)

# Include directories
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)

# Core library
add_subdirectory(core)

# Workers (executables)
if(CPLOAD_BUILD_WORKERS)
    add_subdirectory(workers)
endif()

# Python bindings
if(CPLOAD_BUILD_PYTHON)
    add_subdirectory(python)
endif()

# Tools
if(CPLOAD_BUILD_TOOLS)
    add_subdirectory(tools)
endif()

# Tests
if(CPLOAD_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# Install targets
install(EXPORT cppload-targets
    FILE cppload-targets.cmake
    NAMESPACE cppload::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/cppload
)

include(CMakePackageConfigHelpers)
write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/cppload-config-version.cmake
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)

configure_package_config_file(
    cmake/cppload-config.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/cppload-config.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/cppload
)

install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/cppload-config.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/cppload-config-version.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/cppload
)
