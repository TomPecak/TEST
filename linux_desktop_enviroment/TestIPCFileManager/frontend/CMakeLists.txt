cmake_minimum_required(VERSION 3.21)
project(TestFilesFrontend LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

find_package(Qt6 6.5 REQUIRED COMPONENTS Gui Quick Qml)
find_package(CapnProto REQUIRED)
qt_standard_project_setup(REQUIRES 6.5)

# Kod z kontraktu (wspólnego z backendem Rust)
set(CAPNPC_SRC_PREFIX ${CMAKE_CURRENT_SOURCE_DIR}/../contract)
set(CAPNPC_OUTPUT_DIR ${CMAKE_CURRENT_BINARY_DIR}/generated)
file(MAKE_DIRECTORY ${CAPNPC_OUTPUT_DIR})
capnp_generate_cpp(CONTRACT_SRCS CONTRACT_HDRS ${CMAKE_CURRENT_SOURCE_DIR}/../contract/files.capnp)

qt_add_executable(test-files
    src/main.cpp
    src/ServiceConnection.h src/ServiceConnection.cpp
    ${CONTRACT_SRCS} ${CONTRACT_HDRS}
)
target_include_directories(test-files PRIVATE src ${CAPNPC_OUTPUT_DIR})

set_source_files_properties(qml/Theme.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)

file(GLOB ICONS RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} qml/assets/icons/*.svg)

qt_add_qml_module(test-files
    URI TestFiles
    VERSION 1.0
    RESOURCE_PREFIX /qt/qml
    SOURCES
        src/FilesBackend.h src/FilesBackend.cpp
        src/FolderModel.h src/FolderModel.cpp
    QML_FILES
        qml/Main.qml
        qml/Theme.qml
        qml/components/LIcon.qml
        qml/components/LMenu.qml
        qml/components/LMenuItem.qml
        qml/components/LMenuSeparator.qml
        qml/components/LScrollBar.qml
        qml/components/LText.qml
        qml/components/SubtleButton.qml
        qml/components/WheelScroller.qml
        qml/files/CommandBar.qml
        qml/files/DetailsPane.qml
        qml/files/ExplorerButton.qml
        qml/files/ExplorerTab.qml
        qml/files/ExplorerToolTip.qml
        qml/files/FileExplorerWindow.qml
        qml/files/FileView.qml
        qml/files/NavigationBar.qml
        qml/files/Sidebar.qml
        qml/files/StatusBar.qml
        qml/files/TabStrip.qml
    RESOURCES
        ${ICONS}
)

target_link_libraries(test-files PRIVATE
    Qt6::Gui Qt6::Quick Qt6::Qml
    CapnProto::capnp-rpc CapnProto::capnp CapnProto::kj-async CapnProto::kj
)

# Testy modelu (bez usługi): cmake --build build --target folder-model-test && build/folder-model-test
find_package(Qt6 REQUIRED COMPONENTS Test)
qt_add_executable(folder-model-test
    tests/FolderModelTest.cpp
    src/ServiceConnection.h src/ServiceConnection.cpp
    src/FilesBackend.h src/FilesBackend.cpp
    src/FolderModel.h src/FolderModel.cpp
    ${CONTRACT_SRCS} ${CONTRACT_HDRS}
)
target_include_directories(folder-model-test PRIVATE src ${CAPNPC_OUTPUT_DIR})
target_link_libraries(folder-model-test PRIVATE
    Qt6::Gui Qt6::Qml Qt6::Test
    CapnProto::capnp-rpc CapnProto::capnp CapnProto::kj-async CapnProto::kj
)
