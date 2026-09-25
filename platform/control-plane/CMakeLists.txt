# @author ssrjkk | volley
# Control plane executable

add_executable(volley-control-plane main.cpp)

target_link_libraries(volley-control-plane
    PRIVATE cppload_platform
)

install(TARGETS volley-control-plane
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
