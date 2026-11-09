# SWOS tools -- cross-platform C++ port of the Delphi SWOS 2020 suite.
#
# One project, one shared core, several apps:
#
#   swos-core         window/GL/Slint hosting, ini, settings, DLC model, http
#   swos-launcher     the game launcher      (miniLauncher_u.pas)
#   swos-dlc-manager  the DLC manager        (contents_downloader_u.pas)
#
# The core is shared rather than duplicated because the Delphi build shares it
# too: the launcher applies DLC before starting the game, and both programs
# read and write the same ini/gameLauncher.ini. See docs/ARCHITECTURE.md.

cmake_minimum_required(VERSION 3.21)
project(swos-tools VERSION 0.1.0 LANGUAGES CXX)

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

# Homebrew keeps curl and libarchive keg-only because macOS ships older ones;
# point CMake at them so a plain `brew install` is enough.
if(APPLE)
    find_program(SWOS_BREW brew)
    if(SWOS_BREW)
        foreach(_keg curl libarchive)
            execute_process(COMMAND ${SWOS_BREW} --prefix ${_keg}
                            OUTPUT_VARIABLE _prefix OUTPUT_STRIP_TRAILING_WHITESPACE
                            ERROR_QUIET)
            if(_prefix)
                list(APPEND CMAKE_PREFIX_PATH "${_prefix}")
                set(ENV{PKG_CONFIG_PATH} "${_prefix}/lib/pkgconfig:$ENV{PKG_CONFIG_PATH}")
            endif()
        endforeach()
    endif()
endif()

# --- Slint ---------------------------------------------------------------
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

# The prebuilt package advertises which Cargo features it was built with but
# does not turn them into compile definitions, and the public headers gate whole
# classes on them (SoftwareRenderer lives behind SLINT_FEATURE_RENDERER_SOFTWARE).
# Slint::Slint is an INTERFACE target, so it has no TARGET_FILE. The actual
# shared library is the imported slint_cpp-shared that SlintConfig.cmake
# defines; a from-source static Slint has no such target, hence the guard at
# each use below.
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
        "-DSLINT_FEATURE_RENDERER_SOFTWARE=ON -- these apps composite the UI\n"
        "into their own GL context and never let Slint own a window.")
endif()

# --- SDL2 ----------------------------------------------------------------
find_package(SDL2 CONFIG QUIET)
find_package(PkgConfig QUIET)
if(NOT SDL2_FOUND AND PkgConfig_FOUND)
    pkg_check_modules(SDL2 IMPORTED_TARGET sdl2)
    if(SDL2_FOUND)
        add_library(SDL2::SDL2 ALIAS PkgConfig::SDL2)
    endif()
endif()
if(NOT SDL2_FOUND)
    message(FATAL_ERROR "SDL2 not found. Install libsdl2-dev / brew install sdl2 / vcpkg install sdl2.")
endif()

# --- OpenGL --------------------------------------------------------------
set(OpenGL_GL_PREFERENCE GLVND)
find_package(OpenGL REQUIRED)

# --- libcurl (DLC downloads) ---------------------------------------------
find_package(CURL REQUIRED)

# --- libarchive (7z extraction) ------------------------------------------
find_package(LibArchive)
if(NOT LibArchive_FOUND AND PkgConfig_FOUND)
    pkg_check_modules(LibArchive REQUIRED IMPORTED_TARGET libarchive)
    add_library(LibArchive::LibArchive ALIAS PkgConfig::LibArchive)
elseif(LibArchive_FOUND AND NOT TARGET LibArchive::LibArchive)
    # FindLibArchive only defines the imported target from CMake 3.17 with some
    # generators; synthesise it so the link line below is uniform.
    add_library(LibArchive::LibArchive UNKNOWN IMPORTED)
    set_target_properties(LibArchive::LibArchive PROPERTIES
        IMPORTED_LOCATION "${LibArchive_LIBRARIES}"
        INTERFACE_INCLUDE_DIRECTORIES "${LibArchive_INCLUDE_DIRS}")
endif()

# --- SDL2_mixer (DLC audio preview) --------------------------------------
# Optional: without it the DLC manager still browses, downloads and installs,
# it just cannot play a commentary or music pack before you commit to it.
find_package(SDL2_mixer CONFIG QUIET)
if(NOT SDL2_mixer_FOUND AND PkgConfig_FOUND)
    pkg_check_modules(SDL2_MIXER IMPORTED_TARGET SDL2_mixer)
    if(SDL2_MIXER_FOUND)
        add_library(SDL2_mixer::SDL2_mixer ALIAS PkgConfig::SDL2_MIXER)
        set(SDL2_mixer_FOUND TRUE)
    endif()
endif()
if(SDL2_mixer_FOUND)
    message(STATUS "SDL2_mixer found -- DLC audio preview enabled")
else()
    message(WARNING "SDL2_mixer not found -- DLC audio preview will be disabled")
endif()

# ---------------------------------------------------------------------------
# swos-core
# ---------------------------------------------------------------------------

add_library(swos-core STATIC
    core/src/gl_core33.cpp
    core/src/window.cpp
    core/src/slint_gl_platform.cpp
    core/src/ini_file.cpp
    core/src/settings.cpp
    core/src/game_launch.cpp
    core/src/paths.cpp
    core/src/system_theme.cpp
    core/src/http.cpp
    core/src/archive_extract.cpp
    core/src/dlc_catalog.cpp
    core/src/json.cpp
    core/src/sha256.cpp
    core/src/mod_store.cpp
)

target_include_directories(swos-core PUBLIC core/include)
target_link_libraries(swos-core
    PUBLIC Slint::Slint SDL2::SDL2 OpenGL::GL
    PRIVATE CURL::libcurl LibArchive::LibArchive)

if(APPLE)
    target_link_libraries(swos-core PRIVATE "-framework CoreFoundation")
endif()

if(MSVC)
    target_compile_options(swos-core PRIVATE /W4 /permissive- /utf-8)
else()
    target_compile_options(swos-core PRIVATE -Wall -Wextra -Wno-unused-parameter)
endif()

# ---------------------------------------------------------------------------
# Apps
# ---------------------------------------------------------------------------

set(SWOS_ICON_DIR "${CMAKE_CURRENT_SOURCE_DIR}/assets/icon")

# The macOS bundle needs a .icns, which only `iconutil` can produce. Generate it
# at configure time from the committed .iconset if it is not already there, so a
# Mac checkout needs no extra step. See scripts/generate_icons.py.
if(APPLE)
    set(SWOS_ICNS "${SWOS_ICON_DIR}/swos.icns")
    if(NOT EXISTS "${SWOS_ICNS}")
        find_program(SWOS_ICONUTIL iconutil)
        if(SWOS_ICONUTIL AND EXISTS "${SWOS_ICON_DIR}/swos.iconset")
            message(STATUS "Building swos.icns from swos.iconset")
            execute_process(COMMAND ${SWOS_ICONUTIL} -c icns
                                    "${SWOS_ICON_DIR}/swos.iconset" -o "${SWOS_ICNS}"
                            RESULT_VARIABLE _icns_result)
            if(NOT _icns_result EQUAL 0)
                message(WARNING "iconutil failed; the bundle will have no icon")
            endif()
        endif()
    endif()
endif()

# Shared setup for a Slint app built on swos-core.
function(swos_add_app target)
    cmake_parse_arguments(APP "" "SLINT;DISPLAY_NAME" "SOURCES" ${ARGN})

    # Windows: the .rc is what attaches the icon to the exe, and it must be
    # compiled into the target rather than linked in afterwards.
    set(_platform_sources "")
    if(WIN32)
        set(_rc "${CMAKE_CURRENT_BINARY_DIR}/${target}.rc")
        # The .rc references swos.ico relative to itself, so copy both next to
        # each other in the build tree instead of rewriting the path.
        configure_file("${CMAKE_CURRENT_SOURCE_DIR}/packaging/${target}.rc" "${_rc}" COPYONLY)
        configure_file("${SWOS_ICON_DIR}/swos.ico"
                       "${CMAKE_CURRENT_BINARY_DIR}/swos.ico" COPYONLY)
        list(APPEND _platform_sources "${_rc}")
    elseif(APPLE AND EXISTS "${SWOS_ICON_DIR}/swos.icns")
        # Listing the .icns as a source with MACOSX_PACKAGE_LOCATION puts it in
        # Contents/Resources; Info.plist then names it.
        list(APPEND _platform_sources "${SWOS_ICON_DIR}/swos.icns")
        set_source_files_properties("${SWOS_ICON_DIR}/swos.icns"
            PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
    endif()

    add_executable(${target} ${APP_SOURCES} ${_platform_sources})

    # Compile the icons into the binary. The default, as-absolute-path, stores
    # the build machine's own paths, so a copied or installed app silently loses
    # every icon -- verified by `strings` on the Linux build before this was set.
    set_property(TARGET ${target} PROPERTY SLINT_EMBED_RESOURCES embed-files)
    target_link_libraries(${target} PRIVATE swos-core)
    slint_target_sources(${target} ${APP_SLINT})

    if(WIN32)
        set_target_properties(${target} PROPERTIES WIN32_EXECUTABLE TRUE)
        if(TARGET SDL2::SDL2main)
            target_link_libraries(${target} PRIVATE SDL2::SDL2main)
        endif()
        if(TARGET slint_cpp-shared)
            add_custom_command(TARGET ${target} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    $<TARGET_FILE:slint_cpp-shared> $<TARGET_FILE_DIR:${target}>
                COMMAND_EXPAND_LISTS)
        endif()
    elseif(APPLE)
        set_target_properties(${target} PROPERTIES
            MACOSX_BUNDLE TRUE
            MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/packaging/Info.plist.in"
            MACOSX_BUNDLE_BUNDLE_NAME "${APP_DISPLAY_NAME}"
            MACOSX_BUNDLE_GUI_IDENTIFIER "de.sensiblesoccer.${target}"
            MACOSX_BUNDLE_ICON_FILE "swos.icns"
            MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
            MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
            # Inside a bundle the executable sits in Contents/MacOS, so the
            # Slint dylib beside it and in Frameworks both have to be findable.
            # BUILD_RPATH as well as INSTALL_RPATH: the latter only applies to
            # an installed binary, so without it running straight out of the
            # build tree dies with "Library not loaded: @rpath/libslint_cpp.dylib".
            BUILD_RPATH "@executable_path;@executable_path/../Frameworks;${SWOS_SLINT_ROOT}/lib"
            INSTALL_RPATH "@executable_path;@executable_path/../Frameworks")
        # Put the Slint runtime in the bundle so it runs off this machine.
        if(TARGET slint_cpp-shared)
            add_custom_command(TARGET ${target} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E make_directory
                    "$<TARGET_BUNDLE_CONTENT_DIR:${target}>/Frameworks"
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    $<TARGET_FILE:slint_cpp-shared>
                    "$<TARGET_BUNDLE_CONTENT_DIR:${target}>/Frameworks"
                COMMAND_EXPAND_LISTS)
        endif()
    else()
        set_target_properties(${target} PROPERTIES
            INSTALL_RPATH "$ORIGIN"
            BUILD_RPATH "${SWOS_SLINT_ROOT}/lib")
    endif()

    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /permissive- /utf-8)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wno-unused-parameter)
    endif()

    install(TARGETS ${target}
            RUNTIME DESTINATION .
            BUNDLE  DESTINATION .)

    # Linux desktop integration: the .desktop file plus the icon theme tree, so
    # the app appears in menus with its icon.
    if(UNIX AND NOT APPLE)
        install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/packaging/${target}.desktop"
                DESTINATION share/applications)
        install(DIRECTORY "${SWOS_ICON_DIR}/hicolor"
                DESTINATION share/icons)
    endif()
endfunction()

swos_add_app(swos-launcher
    DISPLAY_NAME "SWOS United Launcher"
    SOURCES apps/launcher/main.cpp
    SLINT   apps/launcher/launcher.slint)

swos_add_app(swos-dlc-manager
    DISPLAY_NAME "SWOS United DLC Manager"
    SOURCES apps/dlc_manager/main.cpp apps/dlc_manager/audio_preview.cpp
    SLINT   apps/dlc_manager/dlc_manager.slint)

if(SDL2_mixer_FOUND)
    target_link_libraries(swos-dlc-manager PRIVATE SDL2_mixer::SDL2_mixer)
    target_compile_definitions(swos-dlc-manager PRIVATE SWOS_HAVE_SDL_MIXER=1)
endif()
