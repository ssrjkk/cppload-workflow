// @author ssrjkk | cppload
find_package(gRPC REQUIRED)
find_package(Protobuf REQUIRED)

# Generate gRPC stubs
get_filename_component(ProtoFile "../proto/load_controller.proto" ABSOLUTE)
get_filename_component(ProtoDir "../proto" ABSOLUTE)

set(ProtoSources "${CMAKE_CURRENT_BINARY_DIR}/load_controller.pb.cc")
set(ProtoHeaders "${CMAKE_CURRENT_BINARY_DIR}/load_controller.pb.h")
set(GrpcSources "${CMAKE_CURRENT_BINARY_DIR}/load_controller.grpc.pb.cc")
set(GrpcHeaders "${CMAKE_CURRENT_BINARY_DIR}/load_controller.grpc.pb.h")

add_custom_command(
    OUTPUT "${ProtoSources}" "${ProtoHeaders}" "${GrpcSources}" "${GrpcHeaders}"
    COMMAND ${Protobuf_PROTOC_EXECUTABLE}
    ARGS --cpp_out="${CMAKE_CURRENT_BINARY_DIR}"
         --grpc_out="${CMAKE_CURRENT_BINARY_DIR}"
         --plugin=protoc-gen-grpc="${GRPC_CPP_PLUGIN_EXECUTABLE}"
         -I "${ProtoDir}"
         "${ProtoFile}"
    DEPENDS "${ProtoFile}"
)

set(GeneratedSources ${ProtoSources} ${ProtoHeaders} ${GrpcSources} ${GrpcHeaders})

add_executable(grpc_worker main.cpp ${GeneratedSources})

target_include_directories(grpc_worker
    PRIVATE ${CMAKE_CURRENT_BINARY_DIR}
            ${CMAKE_SOURCE_DIR}/include
)

target_link_libraries(grpc_worker
    PRIVATE cppload_core
            gRPC::grpc++
            protobuf::libprotobuf
)

install(TARGETS grpc_worker
    DESTINATION ${CMAKE_INSTALL_BINDIR}
)