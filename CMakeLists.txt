# SWOS Launcher -- cross-platform C++ port of the Delphi "SWOS 2020 Game Launcher".
#
# SDL2 owns the window and an OpenGL 3.3 core context; Slint draws the UI through
# its software renderer into a CPU buffer that we upload as a GL texture and
# present on a fullscreen quad. See docs/ARCHITECTURE.md.

cmake_minimum_required(VERSION 3.21)
project(swos-launcher VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

# ---------------------------------------------------------------------------
# Dependencies
# ---------------------------------------------------------------------------

# --- Slint ---------------------------------------------------------------
# Default to the prebuilt SDK that scripts/fetch_slint.{sh,ps1} drops into
# third_party/slint. A system-wide or from-source Slint is picked up instead by
# passing -DSlint_DIR=... (or -DSWOS_SLINT_ROOT=...).
set(SWOS_SLINT_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/third_party/slint"
    CACHE PATH "Root of a prebuilt Slint C++ SDK")
if(NOT Slint_DIR AND EXISTS "${SWOS_SLINT_ROOT}/lib/cmake/Slint/SlintConfig.cmake")
    set(Slint_DIR "${SWOS_SLINT_ROOT}/lib/cmake/Slint")
endif()

find_package(Slint QUIET)
if(NOT Slint_FOUND)
    message(FATAL_ERROR
        "Slint was not found.\n"
        "Run scripts/fetch_slint.sh (Linux/macOS) or scripts/fetch_slint.ps1 (Windows)\n"
        "to download the prebuilt SDK into third_party/slint, or configure with\n"
        "-DSlint_DIR=<prefix>/lib/cmake/Slint.")
endif()

# The prebuilt package advertises which Cargo features it was built with but does
# not turn them into compile definitions, and the public headers gate whole
# classes on them (SoftwareRenderer lives behind SLINT_FEATURE_RENDERER_SOFTWARE).
# Translate the list ourselves so a prebuilt and a from-source Slint behave alike.
get_target_property(_slint_features Slint::Slint SLINT_ENABLED_FEATURES)
if(_slint_features)
    foreach(_feature IN LISTS _slint_features)
        target_compile_definitions(Slint::Slint INTERFACE SLINT_FEATURE_${_feature})
    endforeach()
    message(STATUS "Slint features: ${_slint_features}")
endif()

if(NOT "RENDERER_SOFTWARE" IN_LIST _slint_features)
    message(FATAL_ERROR
        "This Slint build lacks the software renderer. Rebuild it with\n"
        "-DSLINT_FEATURE_RENDERER_SOFTWARE=ON -- the launcher composites the UI\n"
        "into its own GL context and never lets Slint own a window.")
endif()

# --- SDL2 ----------------------------------------------------------------
find_package(SDL2 CONFIG QUIET)
if(NOT SDL2_FOUND)
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
        pkg_check_modules(SDL2 IMPORTED_TARGET sdl2)
        if(SDL2_FOUND)
            add_library(SDL2::SDL2 ALIAS PkgConfig::SDL2)
        endif()
    endif()
endif()
if(NOT SDL2_FOUND)
    message(FATAL_ERROR "SDL2 not found. Install libsdl2-dev / brew install sdl2 / vcpkg install sdl2.")
endif()

# --- OpenGL --------------------------------------------------------------
# Only needed for the loader's fallback path; every entry point we use is
# resolved at runtime through SDL_GL_GetProcAddress.
set(OpenGL_GL_PREFERENCE GLVND)
find_package(OpenGL REQUIRED)

# ---------------------------------------------------------------------------
# Target
# ---------------------------------------------------------------------------

add_executable(swos-launcher
    src/main.cpp
    src/gl_core33.cpp
    src/window.cpp
    src/slint_gl_platform.cpp
    src/ini_file.cpp
    src/settings.cpp
    src/game_launch.cpp
    src/paths.cpp
    src/system_theme.cpp
)

target_include_directories(swos-launcher PRIVATE include)
target_link_libraries(swos-launcher PRIVATE Slint::Slint SDL2::SDL2 OpenGL::GL)

# Slint compiles ui/launcher.slint into a generated header included as
# "launcher_ui.h" -- see src/main.cpp.
slint_target_sources(swos-launcher ui/launcher.slint)

if(WIN32)
    # The launcher is a GUI app: no console window on Windows.
    set_target_properties(swos-launcher PROPERTIES WIN32_EXECUTABLE TRUE)
    target_link_libraries(swos-launcher PRIVATE SDL2::SDL2main)
    # Ship the Slint runtime next to the exe.
    add_custom_command(TARGET swos-launcher POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:Slint::Slint> $<TARGET_FILE_DIR:swos-launcher>
        COMMAND_EXPAND_LISTS)
elseif(APPLE)
    # CoreFoundation: reading AppleInterfaceStyle for the system light/dark mode.
    target_link_libraries(swos-launcher PRIVATE "-framework CoreFoundation")
    set_target_properties(swos-launcher PROPERTIES
        INSTALL_RPATH "@executable_path;@executable_path/../Frameworks")
else()
    set_target_properties(swos-launcher PROPERTIES
        INSTALL_RPATH "$ORIGIN"
        BUILD_RPATH "${SWOS_SLINT_ROOT}/lib")
endif()

if(MSVC)
    target_compile_options(swos-launcher PRIVATE /W4 /permissive- /utf-8)
else()
    target_compile_options(swos-launcher PRIVATE -Wall -Wextra -Wno-unused-parameter)
endif()

install(TARGETS swos-launcher RUNTIME DESTINATION .)
