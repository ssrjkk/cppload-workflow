# @author ssrjkk | volley
# Platform — control plane library + executable

add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../core/platform
                 ${CMAKE_CURRENT_BINARY_DIR}/platform_lib)

if(VOLLEY_BUILD_TOOLS)
    add_subdirectory(control-plane)
endif()
