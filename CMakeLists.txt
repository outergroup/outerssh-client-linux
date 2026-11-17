cmake_minimum_required(VERSION 3.24)
project(OuterSSHClient LANGUAGES C)

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)

add_subdirectory(OuterSSH/OuterSSH)

find_package(PkgConfig REQUIRED)
pkg_check_modules(GTK REQUIRED IMPORTED_TARGET gtk4>=4.10)
pkg_check_modules(WEBKIT REQUIRED IMPORTED_TARGET webkitgtk-6.0>=2.42)

add_executable(outerssh-client src/main.c)
set(HELPER_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/helpers" CACHE PATH
    "Directory holding outerssh-helper-<platform>.gz files")
target_compile_definitions(outerssh-client PRIVATE HELPER_DIRECTORY="${HELPER_DIRECTORY}"
    SOCKET_HOST_DISPLAY_SCRIPT="${CMAKE_CURRENT_SOURCE_DIR}/OuterSSH/OuterSSH/web/SocketHostDisplay.js")
target_compile_options(outerssh-client PRIVATE -Wall -Wextra)
target_link_libraries(outerssh-client PRIVATE OuterSSH::OuterSSH PkgConfig::GTK PkgConfig::WEBKIT)
