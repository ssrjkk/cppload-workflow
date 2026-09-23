# @author ssrjkk | volley
add_executable(volley main.cpp)
target_include_directories(volley PRIVATE ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(volley PRIVATE cppload_core)

install(TARGETS volley DESTINATION ${CMAKE_INSTALL_BINDIR})
