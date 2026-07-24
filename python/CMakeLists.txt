// @author ssrjkk | cppload
find_package(pybind11 REQUIRED)

pybind11_add_module(_cppload MODULE
    bindings.cpp
)

target_include_directories(_cppload
    PRIVATE ${CMAKE_SOURCE_DIR}/include
)

target_link_libraries(_cppload
    PRIVATE cppload_core
)

install(TARGETS _cppload
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cppload
)

install(FILES
    ${CMAKE_CURRENT_SOURCE_DIR}/cppload/__init__.py
    ${CMAKE_CURRENT_SOURCE_DIR}/cppload/core.py
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cppload
)