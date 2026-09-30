cmake_minimum_required(VERSION 3.22)

# JUCE 9 requires C to be enabled alongside C++ (it builds its bundled zlib/libpng/flac as C).
project(ReampForge VERSION 0.1.0 LANGUAGES C CXX)

if(APPLE)
    enable_language(OBJCXX)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version")

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

#===============================================================================
# Workaround for a broken Command Line Tools install: an old, partial libc++ header
# directory left at <CLT>/usr/include/c++/v1 shadows the SDK's complete libc++ headers,
# so even `#include <algorithm>` fails. Only kicks in when that exact situation is
# detected. The proper fix is to remove that stale directory or reinstall the CLT.
# The flags are also exported through the environment because JUCE builds its juceaide
# helper in a separate CMake configure step that does not inherit CMAKE_CXX_FLAGS.
if(APPLE AND CMAKE_OSX_SYSROOT)
    get_filename_component(_rf_toolchain_dir "${CMAKE_CXX_COMPILER}" DIRECTORY)
    set(_rf_stale_libcxx "${_rf_toolchain_dir}/../include/c++/v1")
    set(_rf_sdk_libcxx   "${CMAKE_OSX_SYSROOT}/usr/include/c++/v1")

    if(EXISTS "${_rf_stale_libcxx}" AND NOT EXISTS "${_rf_stale_libcxx}/algorithm"
       AND EXISTS "${_rf_sdk_libcxx}/algorithm")
        message(WARNING "Stale libc++ headers found in ${_rf_stale_libcxx}; using the SDK's libc++ "
                        "headers instead. Remove that directory (or reinstall the Command Line Tools) "
                        "to make this workaround unnecessary.")
        set(_rf_libcxx_flags "-nostdinc++ -isystem ${_rf_sdk_libcxx}")
        string(APPEND CMAKE_CXX_FLAGS    " ${_rf_libcxx_flags}")
        string(APPEND CMAKE_OBJCXX_FLAGS " ${_rf_libcxx_flags}")
        set(ENV{CXXFLAGS}    "$ENV{CXXFLAGS} ${_rf_libcxx_flags}")
        set(ENV{OBJCXXFLAGS} "$ENV{OBJCXXFLAGS} ${_rf_libcxx_flags}")
    endif()
endif()

#===============================================================================
# JUCE (fetched at configure time; nothing needs to be installed)
include(FetchContent)

set(REAMPFORGE_JUCE_VERSION 9.0.3)

FetchContent_Declare(JUCE
    URL      https://github.com/juce-framework/JUCE/archive/refs/tags/${REAMPFORGE_JUCE_VERSION}.tar.gz
    URL_HASH SHA256=a81e5508b8a0efa483917794ebeaff56aed3075730c405a947734413f40c1aba
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)

FetchContent_MakeAvailable(JUCE)

#===============================================================================
# Embedded fonts (SIL OFL, see Assets/Fonts/*-OFL.txt)
juce_add_binary_data(ReampForgeAssets
    HEADER_NAME BinaryData.h
    NAMESPACE   BinaryData
    SOURCES
        Assets/Fonts/JetBrainsMono-Regular.ttf
        Assets/Fonts/JetBrainsMono-Medium.ttf
        Assets/Fonts/JetBrainsMono-Bold.ttf
        Assets/Fonts/Inter-Regular.ttf
        Assets/Fonts/Inter-Medium.ttf
        Assets/Fonts/Inter-Bold.ttf)

set_target_properties(ReampForgeAssets PROPERTIES POSITION_INDEPENDENT_CODE ON)

#===============================================================================
# Model sources (no GUI, no audio thread). Compiled into both the app and the tests;
# JUCE modules are compiled per target, so a shared static library would duplicate them.
set(REAMPFORGE_MODEL_SOURCES
    Source/Model/FileItem.cpp
    Source/Model/FileTree.cpp
    Source/Model/FolderScanner.cpp)

#===============================================================================
# Application
juce_add_gui_app(ReampForge
    PRODUCT_NAME                  "Reamp Forge"
    COMPANY_NAME                  "Reamp Forge"
    BUNDLE_ID                     com.reampforge.app
    VERSION                       ${PROJECT_VERSION}
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT    "Reamp Forge records the audio interface input that returns from your amplifier."
    NEEDS_WEB_BROWSER             FALSE
    NEEDS_CURL                    FALSE)

target_sources(ReampForge PRIVATE
    Source/Main.cpp
    Source/App/CommandLine.cpp
    Source/App/MainComponent.cpp
    Source/App/MainWindow.cpp
    Source/App/Settings.cpp
    Source/App/Snapshot.cpp
    Source/UI/Fonts.cpp
    Source/UI/LookAndFeel.cpp
    Source/UI/TopBar.cpp
    Source/UI/FileTreeView.cpp
    Source/UI/SidebarSection.cpp
    Source/UI/SidebarSections.cpp
    Source/UI/Sidebar.cpp
    Source/UI/WaveformPanel.cpp
    Source/UI/StatusBar.cpp
    ${REAMPFORGE_MODEL_SOURCES})

if(APPLE)
    target_sources(ReampForge PRIVATE Source/App/MacAppearance.mm)
endif()

target_include_directories(ReampForge PRIVATE Source)

target_compile_definitions(ReampForge PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:ReampForge,JUCE_PRODUCT_NAME>"
    JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:ReampForge,JUCE_VERSION>")

# The audio device module is linked now so the toolchain is proven for phase 3.
target_link_libraries(ReampForge
    PRIVATE
        ReampForgeAssets
        juce::juce_gui_basics
        juce::juce_gui_extra
        juce::juce_audio_basics
        juce::juce_audio_devices
        juce::juce_audio_formats
        juce::juce_audio_utils
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# Ad-hoc sign the whole bundle (binary + Info.plist + resources) for local runs.
if(APPLE)
    add_custom_command(TARGET ReampForge POST_BUILD
        COMMAND codesign --force --deep --sign - "$<TARGET_BUNDLE_DIR:ReampForge>"
        COMMENT "Ad-hoc signing Reamp Forge.app"
        VERBATIM)
endif()

#===============================================================================
# Tests (JUCE UnitTest, run through ctest). See ARCHITECTURE.md, "Tests".
option(REAMPFORGE_BUILD_TESTS "Build the unit tests" ON)

if(REAMPFORGE_BUILD_TESTS)
    enable_testing()

    juce_add_console_app(ReampForgeTests
        PRODUCT_NAME "Reamp Forge Tests")

    target_sources(ReampForgeTests PRIVATE
        Tests/TestMain.cpp
        Tests/FileTreeTests.cpp
        Tests/FolderScannerTests.cpp
        Tests/FileTreeViewTests.cpp
        Source/UI/Fonts.cpp
        Source/UI/LookAndFeel.cpp
        Source/UI/FileTreeView.cpp
        ${REAMPFORGE_MODEL_SOURCES})

    target_include_directories(ReampForgeTests PRIVATE Source Tests)

    target_compile_definitions(ReampForgeTests PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0)

    target_link_libraries(ReampForgeTests
        PRIVATE
            ReampForgeAssets
            juce::juce_core
            juce::juce_events
            juce::juce_audio_basics
            juce::juce_audio_formats
            juce::juce_gui_basics
        PUBLIC
            juce::juce_recommended_config_flags
            juce::juce_recommended_warning_flags)

    foreach(_rf_category FolderScanner FileTree FileTreeView)
        add_test(NAME ${_rf_category}
                 COMMAND ReampForgeTests --category=${_rf_category})
    endforeach()
endif()
