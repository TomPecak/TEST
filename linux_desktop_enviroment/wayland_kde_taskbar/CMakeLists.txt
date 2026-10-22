cmake_minimum_required(VERSION 3.16)
project(minitaskbar LANGUAGES CXX)

include(GNUInstallDirs)
find_package(Qt6 6.5 REQUIRED COMPONENTS Gui Qml Quick)
qt_standard_project_setup(REQUIRES 6.5)

qt_add_executable(minitaskbar main.cpp)

qt_add_qml_module(minitaskbar
    URI MiniTaskbar
    VERSION 1.0
    QML_FILES Main.qml
)

# libtaskmanager ładujemy jako moduł QML (import org.kde.taskmanager),
# więc nie trzeba linkować z PW::LibTaskManager.
target_link_libraries(minitaskbar PRIVATE Qt6::Gui Qt6::Qml Qt6::Quick)

# Plik .desktop z uprawnieniami do protokołów KWin.
# Exec musi wskazywać na zainstalowaną binarkę.
configure_file(minitaskbar.desktop.in minitaskbar.desktop @ONLY)

install(TARGETS minitaskbar RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/minitaskbar.desktop
        DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
