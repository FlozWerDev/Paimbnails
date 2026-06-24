cmake_minimum_required(VERSION 3.21)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if ("${CMAKE_SYSTEM_NAME}" STREQUAL "iOS" OR IOS)
    set(CMAKE_OSX_ARCHITECTURES "arm64")
else()
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64")
endif()

set(CMAKE_CXX_VISIBILITY_PRESET hidden)

project(Paimbnails VERSION 1.0.7)

# CPM
set(CPM_DOWNLOAD_VERSION 0.40.2)
set(CPM_DOWNLOAD_LOCATION "${CMAKE_BINARY_DIR}/cmake/CPM_${CPM_DOWNLOAD_VERSION}.cmake")

if (NOT EXISTS ${CPM_DOWNLOAD_LOCATION})
    message(STATUS "Descargando CPM.cmake v${CPM_DOWNLOAD_VERSION}...")
    file(DOWNLOAD
        "https://github.com/cpm-cmake/CPM.cmake/releases/download/v${CPM_DOWNLOAD_VERSION}/CPM.cmake"
        ${CPM_DOWNLOAD_LOCATION}
    )
endif()

include(${CPM_DOWNLOAD_LOCATION})

# pl_mpeg — header-only video decoder fallback
CPMAddPackage(
    NAME pl_mpeg
    GITHUB_REPOSITORY phoboslab/pl_mpeg
    GIT_TAG master
    DOWNLOAD_ONLY YES
)

if (pl_mpeg_ADDED)
    add_library(pl_mpeg INTERFACE)
    target_include_directories(pl_mpeg INTERFACE ${pl_mpeg_SOURCE_DIR})
endif()

# RapidFuzz — fuzzy string matching for Paimon guide NLP (MIT)
CPMAddPackage(
    NAME rapidfuzz
    GITHUB_REPOSITORY rapidfuzz/rapidfuzz-cpp
    GIT_TAG v3.0.5
    DOWNLOAD_ONLY YES
)

if (rapidfuzz_ADDED)
    add_library(rapidfuzz INTERFACE)
    target_include_directories(rapidfuzz INTERFACE ${rapidfuzz_SOURCE_DIR})
endif()

# Sources
file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS src/*.cpp)
if (APPLE)
    file(GLOB_RECURSE OBJCPP_SOURCES CONFIGURE_DEPENDS src/*.mm)
    list(APPEND SOURCES ${OBJCPP_SOURCES})
endif()

add_library(${PROJECT_NAME} SHARED ${SOURCES})

# Optimizaciones
if (MSVC)
    target_compile_options(${PROJECT_NAME} PRIVATE
        /bigobj
        /O2
        /fp:fast
    )
    # /GL + /LTCG disabled: breaks Geode $modify linking (LNK2001 on
    # undefined GD functions). ICF also disabled to preserve function
    # addresses for hook identity and crash symbolication.
    if (CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        target_link_options(${PROJECT_NAME} PRIVATE LINKER:/OPT:NOICF)
    endif()

    # Workaround: MSVC v180 ICE on /O2 for these TUs; /O1 has no measurable cost
    if (CMAKE_CXX_COMPILER_ID STREQUAL "MSVC" AND MSVC_VERSION GREATER_EQUAL 1950)
        set_source_files_properties(
            src/features/community/ui/LeaderboardHistoryLayer.cpp
            src/features/community/ui/LeaderboardLayer.cpp
            src/features/cursor/hooks/CursorHook.cpp
            src/features/cursor/services/CursorManager.cpp
            src/features/cursor/ui/CursorConfigPopup.cpp
            src/features/custom-slider/hooks/SliderThumbHook.cpp
            src/features/custom-slider/services/CustomSliderManager.cpp
            src/features/custom-slider/ui/CustomSliderPopup.cpp
            src/features/discord-presence/DiscordPresenceInit.cpp
            src/features/discord-presence/services/DiscordPresenceManager.cpp
            src/features/discord-presence/ui/DiscordConfigPopup.cpp
            src/features/dynamic-songs/services/DynamicSongManager.cpp
            src/features/editor-music/hooks/EditorMusicEditorHook.cpp
            src/features/editor-music/services/EditorMusicPlayer.cpp
            src/features/editor-music/ui/EditorMusicCell.cpp
            src/features/emotes/EmoteRenderer.cpp
            src/features/emotes/services/EmoteCache.cpp
            PROPERTIES COMPILE_OPTIONS "/O1"
        )
    endif()
else()
    target_compile_options(${PROJECT_NAME} PRIVATE
        -O3
        -ffast-math
        -fvisibility=hidden
        -ffunction-sections
        -fdata-sections
    )
    if (NOT APPLE)
        target_link_options(${PROJECT_NAME} PRIVATE
            -Wl,--gc-sections
        )
    endif()
    # ARM32 Android: enable NEON.  arm64 always has NEON.
    if (ANDROID AND ANDROID_ABI STREQUAL "armeabi-v7a")
        target_compile_options(${PROJECT_NAME} PRIVATE -mfpu=neon)
    endif()
endif()

# Defines
target_compile_definitions(${PROJECT_NAME} PRIVATE
    VIDEO_RING_BUFFER_SIZE=8
    $<$<PLATFORM_ID:Windows>:USE_MEDIA_FOUNDATION=1>
    $<$<PLATFORM_ID:Android>:USE_MEDIA_NDK=1>
    $<$<BOOL:${APPLE}>:USE_AV_FOUNDATION=1>
)

# Libraries
if (WIN32)
    target_link_libraries(${PROJECT_NAME}
        mfplat.lib
        mfreadwrite.lib
        mfuuid.lib
        mf.lib
        mfplay.lib
        evr.lib
        d3d11.lib
        dxgi.lib
        ole32.lib
        shell32.lib
        winmm.lib
        opengl32.lib
    )
elseif (ANDROID)
    target_link_libraries(${PROJECT_NAME}
        mediandk
        GLESv2
        EGL
        android
        log
    )
    # AAudio requires API level 26+; skip it on older targets (e.g. armv7 API 23)
    if (ANDROID_NATIVE_API_LEVEL GREATER_EQUAL 26)
        target_link_libraries(${PROJECT_NAME} aaudio)
    endif()
elseif (APPLE)
    set_source_files_properties(${OBJCPP_SOURCES} PROPERTIES
        COMPILE_FLAGS "-fobjc-arc"
        SKIP_PRECOMPILE_HEADERS ON
    )
    target_link_libraries(${PROJECT_NAME}
        "-framework VideoToolbox"
        "-framework AVFoundation"
        "-framework CoreVideo"
        "-framework CoreMedia"
        "-framework CoreFoundation"
        "-framework CoreGraphics"
        "-framework AudioToolbox"
        "-framework Metal"
    )
    if (IOS)
        target_link_libraries(${PROJECT_NAME} "-framework UIKit")
    else()
        target_link_libraries(${PROJECT_NAME} "-framework Cocoa")
    endif()
endif()

target_link_libraries(${PROJECT_NAME} pl_mpeg)
target_link_libraries(${PROJECT_NAME} rapidfuzz)

if (WIN32 OR APPLE)
    CPMAddPackage("gh:EclipseMenu/discord-presence#3b50b08")
    target_link_libraries(${PROJECT_NAME} discord-rpc)
    target_compile_definitions(${PROJECT_NAME} PRIVATE PAIMON_HAS_DISCORD_RPC=1)
endif()

# Geode SDK
if (NOT DEFINED ENV{GEODE_SDK})
    message(FATAL_ERROR "Unable to find Geode SDK! Please define GEODE_SDK environment variable to point to Geode")
else()
    message(STATUS "Found Geode: $ENV{GEODE_SDK}")
endif()

add_subdirectory($ENV{GEODE_SDK} ${CMAKE_CURRENT_BINARY_DIR}/geode)

setup_geode_mod(${PROJECT_NAME})

message(STATUS "Paimbnails: ${CMAKE_SYSTEM_NAME} | HW decoder: native | SW fallback: pl_mpeg")
