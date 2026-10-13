cmake_minimum_required(VERSION 3.21)
project(SlintCppDemo LANGUAGES CXX)

# Slint wymaga standardu C++20
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Pobranie Slint z GitHuba
include(FetchContent)
FetchContent_Declare(
    Slint
    GIT_REPOSITORY https://github.com/slint-ui/slint.git
    GIT_TAG        v1.18.1
    SOURCE_SUBDIR  api/cpp
)
FetchContent_MakeAvailable(Slint)

# 1. Najpierw tworzymy plik wykonywalny z kodu C++
add_executable(slint_cpp_demo main.cpp)

# 2. Łączymy program z główną biblioteką Slint
target_link_libraries(slint_cpp_demo PRIVATE Slint::Slint)

# 3. Na koniec podpinamy plik z interfejsem graficznym! (To zastępuje stare slint_compile_ui)
slint_target_sources(slint_cpp_demo app_window.slint)
