# Core library sources
set(CORE_SOURCES
    net/http_client.cpp
    metrics/collector.cpp
    metrics/prometheus_exporter.cpp
)

set(CORE_HEADERS
    ${CMAKE_SOURCE_DIR}/include/cppload/net/http_client.hpp
    ${CMAKE_SOURCE_DIR}/include/cppload/metrics/collector.hpp
    ${CMAKE_SOURCE_DIR}/include/cppload/metrics/prometheus_exporter.hpp
)

add_library(cppload_core ${CORE_SOURCES} ${CORE_HEADERS})

target_include_directories(cppload_core
    PUBLIC 
        $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    PRIVATE 
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(cppload_core
    PUBLIC 
        Boost::boost
        Boost::system
        OpenSSL::SSL
        OpenSSL::Crypto
        Threads::Threads
        prometheus-cpp::core
        prometheus-cpp::pull
)

target_compile_features(cppload_core PUBLIC cxx_std_20)

# Install library
install(TARGETS cppload_core
    EXPORT cppload-targets
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

# Install headers
install(DIRECTORY ${CMAKE_SOURCE_DIR}/include/cppload
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    FILES_MATCHING PATTERN "*.hpp"
)
