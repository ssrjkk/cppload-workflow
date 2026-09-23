# @author ssrjkk | volley
find_package(pybind11 REQUIRED)

pybind11_add_module(_volley MODULE
    bindings.cpp
)

target_include_directories(_volley
    PRIVATE ${CMAKE_SOURCE_DIR}/include
)

target_link_libraries(_volley
    PRIVATE cppload_core
)

install(TARGETS _volley
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/volley
)

install(FILES
    ${CMAKE_CURRENT_SOURCE_DIR}/volley/__init__.py
    ${CMAKE_CURRENT_SOURCE_DIR}/volley/core.py
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/volley
)