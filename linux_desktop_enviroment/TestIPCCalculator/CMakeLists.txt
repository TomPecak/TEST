# The whole calculator in one build: the service (Rust, backend/) and the window (Qt, frontend/),
# both generated from the contract (contract/calculator.capnp).
#
#   cmake -S . -B build -G Ninja                   # Release by default
#   cmake --build build -j 3                       # cargo + C++
#   ctest --test-dir build                         # cargo test of the service
#   cmake --install build --prefix ~/opt/test-ipc-calculator
#
# Installed layout:
#   bin/test-ipc-calculator                        launcher: starts the service if needed, opens the window
#   bin/test-calculator                            the window (Qt)
#   libexec/test-ipc-calculator/calculator-service the service (not meant to be run by the user)
#
# backend/ and frontend/ stay buildable on their own (cargo, cmake -S frontend), this file only
# puts them together. Rust is built by cmake/MaiaRust.cmake.
cmake_minimum_required(VERSION 3.21)
project(TestIPCCalculator VERSION 0.1.0 LANGUAGES CXX)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake)
include(GNUInstallDirs)
include(CTest)

add_subdirectory(backend)
add_subdirectory(frontend)

# Launcher of the installed calculator (the same job as run.sh, but on the installed files).
# The paths are relative to the launcher, so `cmake --install --prefix …` and moving the installed
# tree keep working.
file(RELATIVE_PATH SERVICE_FROM_BINDIR /${CMAKE_INSTALL_BINDIR} /${CMAKE_INSTALL_LIBEXECDIR}/test-ipc-calculator/calculator-service)
configure_file(scripts/test-ipc-calculator.sh.in ${CMAKE_CURRENT_BINARY_DIR}/test-ipc-calculator @ONLY)
install(PROGRAMS ${CMAKE_CURRENT_BINARY_DIR}/test-ipc-calculator DESTINATION ${CMAKE_INSTALL_BINDIR})
