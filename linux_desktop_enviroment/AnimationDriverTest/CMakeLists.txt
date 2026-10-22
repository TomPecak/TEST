cmake_minimum_required(VERSION 3.16)

project(AnimationDriverTest VERSION 0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 REQUIRED COMPONENTS Quick Qml)

qt_standard_project_setup(REQUIRES 6.5)

qt_add_executable(appAnimationDriverTest
    main.cpp
)

qt_add_qml_module(appAnimationDriverTest
    URI AnimationDriverTest
    QML_FILES
        Main.qml
        FontBrowser.qml
)

target_link_libraries(appAnimationDriverTest
    PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Qml
    Qt6::Quick
)
