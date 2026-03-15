cmake_minimum_required(VERSION 3.20)
project(Ghostty CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Path to the pre-built libghostty import library produced by `zig build`.
# Override with -DGHOSTTY_LIB_DIR=<path> when building.
set(GHOSTTY_LIB_DIR "${CMAKE_SOURCE_DIR}/../zig-out/lib"
    CACHE PATH "Directory containing ghostty.lib (import lib for ghostty.dll)")

# Path to the Ghostty public C header.
set(GHOSTTY_INCLUDE_DIR "${CMAKE_SOURCE_DIR}/../include"
    CACHE PATH "Directory containing ghostty.h")

# Collect all C++ source files.
file(GLOB_RECURSE GHOSTTY_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_SOURCE_DIR}/src/*.cpp"
)

# The Win32 GUI executable (no console window).
add_executable(Ghostty WIN32 ${GHOSTTY_SOURCES})

target_include_directories(Ghostty PRIVATE
    "${GHOSTTY_INCLUDE_DIR}"
    "${CMAKE_SOURCE_DIR}/src"
)

# Import library for ghostty.dll built by Zig.
find_library(GHOSTTY_IMPORT_LIB
    NAMES ghostty libghostty
    PATHS "${GHOSTTY_LIB_DIR}"
    NO_DEFAULT_PATH
)
if(NOT GHOSTTY_IMPORT_LIB)
    message(FATAL_ERROR
        "Could not find ghostty import library in ${GHOSTTY_LIB_DIR}.\n"
        "Run 'zig build -Dtarget=x86_64-windows-gnu -Dapp-runtime=none' first,\n"
        "or set -DGHOSTTY_LIB_DIR=<path>.")
endif()

target_link_libraries(Ghostty PRIVATE
    "${GHOSTTY_IMPORT_LIB}"
    opengl32
    gdi32
    user32
    shell32
    ole32      # CoInitializeEx / CoUninitialize
    dwrite     # DirectWrite font discovery
    shcore     # GetDpiForMonitor
)

# Windows subsystem — no console window in production builds.
set_target_properties(Ghostty PROPERTIES
    WIN32_EXECUTABLE TRUE
)

# Copy ghostty.dll next to Ghostty.exe after build so the executable can find it.
add_custom_command(TARGET Ghostty POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${GHOSTTY_LIB_DIR}/../bin/ghostty.dll"
        "$<TARGET_FILE_DIR:Ghostty>/ghostty.dll"
    COMMENT "Copying ghostty.dll alongside Ghostty.exe"
    VERBATIM
)

# Install rules (used by the WiX packaging step).
install(TARGETS Ghostty RUNTIME DESTINATION .)
install(FILES "${GHOSTTY_LIB_DIR}/../bin/ghostty.dll" DESTINATION .)
install(DIRECTORY "${GHOSTTY_LIB_DIR}/../share/ghostty/shell-integration/"
        DESTINATION "resources/shell-integration")
