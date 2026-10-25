cmake_minimum_required(VERSION 3.21)
project(TestCalculatorFrontend LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

find_package(Qt6 6.5 REQUIRED COMPONENTS Gui Quick Qml ShaderTools)
find_package(CapnProto REQUIRED)
qt_standard_project_setup(REQUIRES 6.5)

# Kod z kontraktu (wspólnego z backendem Rust)
set(CAPNPC_SRC_PREFIX ${CMAKE_CURRENT_SOURCE_DIR}/../contract)
set(CAPNPC_OUTPUT_DIR ${CMAKE_CURRENT_BINARY_DIR}/generated)
file(MAKE_DIRECTORY ${CAPNPC_OUTPUT_DIR})
capnp_generate_cpp(CONTRACT_SRCS CONTRACT_HDRS ${CMAKE_CURRENT_SOURCE_DIR}/../contract/calculator.capnp)

qt_add_executable(test-calculator
    src/main.cpp
    ${CONTRACT_SRCS} ${CONTRACT_HDRS}
)
target_include_directories(test-calculator PRIVATE src ${CAPNPC_OUTPUT_DIR})

set_source_files_properties(qml/Theme.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)
file(GLOB ICONS RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} qml/assets/icons/*.svg)

qt_add_qml_module(test-calculator
    URI TestCalculator
    VERSION 1.0
    RESOURCE_PREFIX /qt/qml
    SOURCES
        src/CalculatorBackend.h src/CalculatorBackend.cpp
    QML_FILES
        qml/Main.qml
        qml/LumenFace.qml
        qml/IPadFace.qml
        qml/CyberFace.qml
        qml/NeonBloom.qml
        qml/Theme.qml
        qml/TitleBar.qml
        qml/CalcButton.qml
        qml/SidePanel.qml
        qml/components/IconButton.qml
        qml/components/LIcon.qml
        qml/components/LScrollBar.qml
        qml/components/LText.qml
        qml/components/LToolTip.qml
        qml/components/SubtleButton.qml
        qml/components/WheelScroller.qml
    RESOURCES
        ${ICONS}
)

# Shaders of the neon bloom (CyberFace): GLSL compiled to .qsb at build time, in the resources as
# :/shaders/<name>.qsb
qt_add_shaders(test-calculator "neon_shaders"
    PREFIX "/"
    FILES
        shaders/blur.frag
        shaders/neon.frag
)

target_link_libraries(test-calculator PRIVATE
    Qt6::Gui Qt6::Quick Qt6::Qml
    CapnProto::capnp-rpc CapnProto::capnp CapnProto::kj-async CapnProto::kj
)

include(GNUInstallDirs)
install(TARGETS test-calculator RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
