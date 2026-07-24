// @author ssrjkk | cppload
add_executable(http_worker main.cpp)

target_include_directories(http_worker
    PRIVATE ${CMAKE_SOURCE_DIR}/include
)

target_link_libraries(http_worker
    PRIVATE cppload_core
)

install(TARGETS http_worker
    DESTINATION ${CMAKE_INSTALL_BINDIR}
)