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
    "Directory holding the outerssh-helper-<platform> files")

# Downloads the OuterSSH server helpers listed in helpers.sha256 into
# HELPER_DIRECTORY and checks each against its hash. Files that are already
# there with the right hash aren't downloaded again.
set(OUTERSSH_HELPER_VERSION 0.5.4)
file(STRINGS helpers.sha256 helper_pins REGEX "^[0-9a-f]+  ")
foreach(pin IN LISTS helper_pins)
    string(REGEX MATCH "^([0-9a-f]+)  (.+)$" pin "${pin}")
    file(DOWNLOAD "https://outerssh.org/download/outerssh/${OUTERSSH_HELPER_VERSION}/${CMAKE_MATCH_2}"
        "${HELPER_DIRECTORY}/${CMAKE_MATCH_2}" EXPECTED_HASH SHA256=${CMAKE_MATCH_1})
endforeach()
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS helpers.sha256)
target_compile_definitions(outerssh-client PRIVATE HELPER_DIRECTORY="${HELPER_DIRECTORY}"
    SOCKET_HOST_DISPLAY_SCRIPT="${CMAKE_CURRENT_SOURCE_DIR}/OuterSSH/OuterSSH/web/SocketHostDisplay.js")
target_compile_options(outerssh-client PRIVATE -Wall -Wextra)
target_link_libraries(outerssh-client PRIVATE OuterSSH::OuterSSH PkgConfig::GTK PkgConfig::WEBKIT)
