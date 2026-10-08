cmake_minimum_required(VERSION 3.16)
project(LocalSocketLatencyTest VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# Wymagane, aby automatycznie kompilować klasy dziedziczące po QObject
set(CMAKE_AUTOMOC ON)

# Wyszukanie Qt6 z modułami Core i Network
find_package(Qt6 COMPONENTS Core Network REQUIRED)
find_package(CapnProto REQUIRED)


# curl -O https://capnproto.org/capnproto-c++-1.5.0.tar.gz
# tar zxf capnproto-c++-1.5.0.tar.gz
# cd capnproto-c++-1.5.0
# ./configure
# make -j6 check
# sudo make install


capnp_generate_cpp(CAPNP_SRCS CAPNP_HDRS latency.capnp)

# Target: server
add_executable(server server.cpp ${CAPNP_SRCS})
target_include_directories(server PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
target_link_libraries(server PRIVATE
    Qt6::Core
    Qt6::Network
    CapnProto::capnp-rpc
    CapnProto::capnp
    CapnProto::kj
)

# Target: client
add_executable(client client.cpp ${CAPNP_SRCS})
target_include_directories(client PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
target_link_libraries(client PRIVATE
    Qt6::Core
    Qt6::Network
    CapnProto::capnp-rpc
    CapnProto::capnp
    CapnProto::kj
)