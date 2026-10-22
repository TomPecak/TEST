cmake_minimum_required(VERSION 3.16)
project(LocalSocketLatencyTest VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# Wymagane, aby automatycznie kompilować klasy dziedziczące po QObject
set(CMAKE_AUTOMOC ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

find_package(Qt6 COMPONENTS Core Network Protobuf Grpc REQUIRED)
# Cap'n Proto 1.5.0 ze źródeł, patrz README.md
find_package(CapnProto REQUIRED)

capnp_generate_cpp(CAPNP_SRCS CAPNP_HDRS latency.capnp)

# Każdy wariant to osobny program; serwery echo (C, Qt) odsyłają 8-bajtowy numer,
# serwery Cap'n Proto i gRPC odpowiadają na wywołanie RPC. Wszystkie klienty robią tę samą próbę
# (src/bench.h) i drukują wyniki w tym samym formacie. Uruchomienie: run_benchmarks.sh

# Surowe gniazda Unix (bez bibliotek)
add_executable(echo_server_c src/echo_server_c.cpp)
add_executable(client_c_blocking src/client_c_blocking.cpp)
add_executable(client_c_epoll src/client_c_epoll.cpp)

# Qt: QLocalServer / QLocalSocket
add_executable(echo_server_qt src/echo_server_qt.cpp)
target_link_libraries(echo_server_qt PRIVATE Qt6::Core Qt6::Network)
add_executable(client_qt src/client_qt.cpp)
target_link_libraries(client_qt PRIVATE Qt6::Core Qt6::Network)

# Cap'n Proto RPC
foreach(target capnp_server capnp_client)
    add_executable(${target} src/${target}.cpp ${CAPNP_SRCS})
    target_include_directories(${target} PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
    target_link_libraries(${target} PRIVATE CapnProto::capnp-rpc CapnProto::capnp CapnProto::kj)
endforeach()

# gRPC: serwer i natywny klient na referencyjnej bibliotece gRPC C++ (libgrpc++-dev),
# klient Qt na QtGrpc (qt6-grpc-dev). Wszystko z jednego latency.proto.
# OpenSSL i ZLIB to zależności gRPC, których jego konfiguracja nie zawsze sama znajduje.
find_package(OpenSSL)
find_package(ZLIB)
set(CMAKE_FIND_PACKAGE_PREFER_CONFIG TRUE)
find_package(Protobuf REQUIRED)
find_package(gRPC REQUIRED)
find_program(PROTOC_EXECUTABLE protoc REQUIRED)

set(GRPC_GENERATED
    "${CMAKE_CURRENT_BINARY_DIR}/latency.pb.h" "${CMAKE_CURRENT_BINARY_DIR}/latency.pb.cc"
    "${CMAKE_CURRENT_BINARY_DIR}/latency.grpc.pb.h" "${CMAKE_CURRENT_BINARY_DIR}/latency.grpc.pb.cc")
add_custom_command(
    OUTPUT ${GRPC_GENERATED}
    COMMAND ${PROTOC_EXECUTABLE}
        --cpp_out "${CMAKE_CURRENT_BINARY_DIR}"
        --grpc_out "${CMAKE_CURRENT_BINARY_DIR}"
        --plugin=protoc-gen-grpc=$<TARGET_FILE:gRPC::grpc_cpp_plugin>
        -I "${CMAKE_CURRENT_SOURCE_DIR}"
        "${CMAKE_CURRENT_SOURCE_DIR}/latency.proto"
    DEPENDS latency.proto
    COMMENT "Generowanie kodu gRPC C++ z latency.proto"
    VERBATIM)
add_library(latency_grpc_cpp STATIC ${GRPC_GENERATED})
target_include_directories(latency_grpc_cpp PUBLIC ${CMAKE_CURRENT_BINARY_DIR})
target_link_libraries(latency_grpc_cpp PUBLIC gRPC::grpc++ protobuf::libprotobuf)

add_executable(grpc_server src/grpc_server.cpp)
target_link_libraries(grpc_server PRIVATE latency_grpc_cpp)
add_executable(grpc_client_cpp src/grpc_client_cpp.cpp)
target_link_libraries(grpc_client_cpp PRIVATE latency_grpc_cpp)

add_executable(qtgrpc_client src/qtgrpc_client.cpp)
qt_add_protobuf(qtgrpc_client PROTO_FILES latency.proto)
qt_add_grpc(qtgrpc_client CLIENT PROTO_FILES latency.proto)
target_link_libraries(qtgrpc_client PRIVATE Qt6::Core Qt6::Protobuf Qt6::Grpc)
