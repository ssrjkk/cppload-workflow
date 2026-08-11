# @author ssrjkk | cppload
add_executable(cppload-cli main.cpp)
target_include_directories(cppload-cli PRIVATE ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(cppload-cli PRIVATE cppload_core)

install(TARGETS cppload-cli DESTINATION ${CMAKE_INSTALL_BINDIR})