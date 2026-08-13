cmake_minimum_required(VERSION 3.21)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if ("${CMAKE_SYSTEM_NAME}" STREQUAL "iOS" OR IOS)
    set(CMAKE_OSX_ARCHITECTURES "arm64")
else()
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64")
endif()

set(CMAKE_CXX_VISIBILITY_PRESET hidden)

project(Paimbnails VERSION 1.1.1)

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

# pl_mpeg — header-only MPEG-1 decoder (.mpg/.mpeg only)
CPMAddPackage(
    NAME pl_mpeg
    GITHUB_REPOSITORY phoboslab/pl_mpeg
    GIT_TAG c871f2be022ece7ef4f64230b4fb8e1fb9eb6023
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

# libyuv — SIMD YUV conversion and scaling (BSD-3)
CPMAddPackage(
    NAME libyuv
    GIT_REPOSITORY https://chromium.googlesource.com/libyuv/libyuv
    GIT_TAG 9c5d5c1e55e8956d6bcd9cabddace4541abde2d3
    DOWNLOAD_ONLY YES
)

if (libyuv_ADDED)
    file(GLOB LIBYUV_SOURCES ${libyuv_SOURCE_DIR}/source/*.cc)
    add_library(yuv STATIC ${LIBYUV_SOURCES})
    target_include_directories(yuv PUBLIC ${libyuv_SOURCE_DIR}/include)
    # No libjpeg here, and SME/SVE need toolchains newer than the Android NDK's.
    target_compile_definitions(yuv PUBLIC
        LIBYUV_DISABLE_JPEG
        LIBYUV_DISABLE_SME
        LIBYUV_DISABLE_SVE
    )
    set_target_properties(yuv PROPERTIES POSITION_INDEPENDENT_CODE ON)
    if (MSVC)
        target_compile_options(yuv PRIVATE /w)
    else()
        target_compile_options(yuv PRIVATE -w)
    endif()

    # row_neon64.cc and compare_neon64.cc use sdot/udot/usdot, which the
    # assembler rejects unless dotprod and i8mm are enabled. Upstream builds the
    # *_neon64.cc files as their own object library with these flags; we glob
    # every source into one target, so they have to be set per file instead.
    # ThinLTO defers inline asm to the link, which is where iOS reports it.
    if (ANDROID)
        if (ANDROID_ABI STREQUAL "arm64-v8a")
            set(LIBYUV_NEON64_FLAGS -march=armv8.2-a+dotprod+i8mm)
        endif()
    elseif (APPLE)
        if (CMAKE_OSX_ARCHITECTURES MATCHES "x86_64")
            # mac builds both slices in one clang pass, so the flag has to be
            # scoped to arm64 or the Intel one fails to parse it.
            set(LIBYUV_NEON64_FLAGS -Xarch_arm64 -march=armv8.2-a+dotprod+i8mm)
        else()
            set(LIBYUV_NEON64_FLAGS -march=armv8.2-a+dotprod+i8mm)
        endif()
    elseif (CMAKE_SYSTEM_PROCESSOR MATCHES "^(aarch64|arm64)$")
        set(LIBYUV_NEON64_FLAGS -march=armv8.2-a+dotprod+i8mm)
    endif()

    if (DEFINED LIBYUV_NEON64_FLAGS)
        set_source_files_properties(
            ${libyuv_SOURCE_DIR}/source/compare_neon64.cc
            ${libyuv_SOURCE_DIR}/source/rotate_neon64.cc
            ${libyuv_SOURCE_DIR}/source/row_neon64.cc
            ${libyuv_SOURCE_DIR}/source/scale_neon64.cc
            PROPERTIES COMPILE_OPTIONS "${LIBYUV_NEON64_FLAGS}"
        )
    endif()
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
        ws2_32.lib
        winmm.lib
        winhttp.lib
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
target_link_libraries(${PROJECT_NAME} yuv)

# Discord Rich Presence uses our own self-contained IPC client
# (src/features/discord-presence/services/DiscordIpcClient.*), so there's no
# external dependency to fetch. Enabled on desktop platforms only.
# APPLE is also true for iOS, where there is no Discord client to open the
# local IPC socket to, so the desktop check has to exclude it explicitly.
if (WIN32 OR (APPLE AND NOT IOS))
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
