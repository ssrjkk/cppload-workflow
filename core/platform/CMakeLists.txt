# @author ssrjkk | volley
# Platform library — control plane business logic

set(PLATFORM_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/in_memory_repo.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/policy.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/http_server.cpp
)

set(PLATFORM_HEADERS
    ${CMAKE_SOURCE_DIR}/include/cppload/platform/models.hpp
    ${CMAKE_SOURCE_DIR}/include/cppload/platform/repository.hpp
    ${CMAKE_SOURCE_DIR}/include/cppload/platform/policy.hpp
    ${CMAKE_SOURCE_DIR}/include/cppload/platform/http_server.hpp
)

add_library(cppload_platform ${PLATFORM_SOURCES} ${PLATFORM_HEADERS})

target_include_directories(cppload_platform
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(cppload_platform
    PUBLIC
        Boost::boost
        Boost::system
        Threads::Threads
        nlohmann_json::nlohmann_json
)

target_compile_features(cppload_platform PUBLIC cxx_std_20)

install(TARGETS cppload_platform
    EXPORT volley-targets
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
