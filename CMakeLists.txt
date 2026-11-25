cmake_minimum_required(VERSION 3.21)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if ("${CMAKE_SYSTEM_NAME}" STREQUAL "iOS" OR IOS)
    set(CMAKE_OSX_ARCHITECTURES "arm64")
else()
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64")
endif()

set(CMAKE_CXX_VISIBILITY_PRESET hidden)

project(Paimbnails VERSION 1.2.6)

# Allow Clang on Windows runners where MSVC STL is newer than the installed LLVM Clang.
if (WIN32 AND CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    add_compile_definitions(_ALLOW_COMPILER_AND_STL_VERSION_MISMATCH)
endif()

# Geometry Dash 2.2091 is supported by Geode SDK v6.0.0 or newer.

# cpm
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

# pl_mpeg — header-only mpeg-1 decoder (.mpg/.mpeg only)
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

# rapidfuzz — fuzzy string matching for paimon guide nlp (mit)
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

# libyuv — simd yuv conversion and scaling (bsd-3)
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
    # no libjpeg here, and sme/sve need toolchains newer than the android ndk's.
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

    # per-file arm64 features match upstream; thinlto emits this asm at link time.
    if (ANDROID)
        if (ANDROID_ABI STREQUAL "arm64-v8a")
            set(LIBYUV_NEON64_FLAGS -march=armv8.2-a+dotprod+i8mm)
        endif()
    elseif (APPLE)
        if (CMAKE_OSX_ARCHITECTURES MATCHES "x86_64")
            # mac builds both slices in one clang pass, so the flag has to be
            # scoped to arm64 or the intel one fails to parse it.
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

# sources
file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS src/*.cpp)
if (APPLE)
    file(GLOB_RECURSE OBJCPP_SOURCES CONFIGURE_DEPENDS src/*.mm)
    list(APPEND SOURCES ${OBJCPP_SOURCES})
endif()

add_library(${PROJECT_NAME} SHARED ${SOURCES})

option(PAIMBNAILS_EXTRA_WARNINGS "Enable extra warnings for the mod during maintenance" OFF)
if (PAIMBNAILS_EXTRA_WARNINGS)
    if (MSVC)
        target_compile_options(${PROJECT_NAME} PRIVATE /W4)
    else()
        target_compile_options(${PROJECT_NAME} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endif()

# preserve nan/inf checks used to validate decoded and user-provided values.
if (MSVC)
    target_compile_options(${PROJECT_NAME} PRIVATE
        /bigobj
        /fp:precise
        $<$<CONFIG:Release>:/O2>
        $<$<CONFIG:RelWithDebInfo>:/O2>
        $<$<CONFIG:MinSizeRel>:/O1>
    )
    # /gl + /ltcg disabled: breaks geode $modify linking (lnk2001 on
    # undefined gd functions). icf also disabled to preserve function
    # addresses for hook identity and crash symbolication.
    if (CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        target_link_options(${PROJECT_NAME} PRIVATE LINKER:/OPT:NOICF)
    endif()

    # workaround: msvc (v17.14/19.44 and v18/19.50+) ice on /o2 for these
    # tus; /o1 has no measurable cost
    if (CMAKE_CXX_COMPILER_ID STREQUAL "MSVC" AND MSVC_VERSION GREATER_EQUAL 1940)
        set_source_files_properties(
            src/features/collab-editor/CollabNetClient.cpp
            src/features/collab-editor/CollabPresence.cpp
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
        -fno-fast-math
        -fvisibility=hidden
        -ffunction-sections
        -fdata-sections
        $<$<CONFIG:Release>:-O3>
        $<$<CONFIG:RelWithDebInfo>:-O2>
        $<$<CONFIG:MinSizeRel>:-Os>
    )
    if (NOT APPLE AND (NOT WIN32 OR MINGW))
        target_link_options(${PROJECT_NAME} PRIVATE
            -Wl,--gc-sections
        )
    endif()
    # arm32 android: enable neon.  arm64 always has neon.
    if (ANDROID AND ANDROID_ABI STREQUAL "armeabi-v7a")
        target_compile_options(${PROJECT_NAME} PRIVATE -mfpu=neon)
    endif()
endif()

# defines
target_compile_definitions(${PROJECT_NAME} PRIVATE
    VIDEO_RING_BUFFER_SIZE=8
    $<$<PLATFORM_ID:Windows>:USE_MEDIA_FOUNDATION=1>
    $<$<PLATFORM_ID:Windows>:PAIM_GIF_COMPUTE=1>
    $<$<PLATFORM_ID:Android>:USE_MEDIA_NDK=1>
    $<$<BOOL:${APPLE}>:USE_AV_FOUNDATION=1>
)

# libraries
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
        windowsapp.lib
        opengl32.lib
        bcrypt.lib
    )
elseif (ANDROID)
    target_link_libraries(${PROJECT_NAME}
        mediandk
        GLESv2
        EGL
        android
        log
    )
    # aaudio requires api level 26+; skip it on older targets (e.g. armv7 api 23)
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
include(cmake/WebWallpaper.cmake)

# discord rich presence uses our own self-contained ipc client
# (src/features/discord-presence/services/discordipcclient.*), so there's no
# external dependency to fetch. enabled on desktop platforms only.
# apple is also true for ios, where there is no discord client to open the
# local ipc socket to, so the desktop check has to exclude it explicitly.
if (WIN32 OR (APPLE AND NOT IOS))
    target_compile_definitions(${PROJECT_NAME} PRIVATE PAIMON_HAS_DISCORD_RPC=1)
endif()

# geode sdk
if (NOT DEFINED ENV{GEODE_SDK})
    message(FATAL_ERROR "Unable to find Geode SDK! Please define GEODE_SDK environment variable to point to Geode")
else()
    message(STATUS "Found Geode: $ENV{GEODE_SDK}")
endif()

if (NOT EXISTS "$ENV{GEODE_SDK}/VERSION")
    message(FATAL_ERROR "Geode SDK VERSION file not found at $ENV{GEODE_SDK}")
endif()
file(READ "$ENV{GEODE_SDK}/VERSION" PAIMBNAILS_GEODE_SDK_VERSION)
string(STRIP "${PAIMBNAILS_GEODE_SDK_VERSION}" PAIMBNAILS_GEODE_SDK_VERSION)
if (PAIMBNAILS_GEODE_SDK_VERSION VERSION_LESS "6.0.0")
    message(FATAL_ERROR "Paimbnails requires Geode SDK 6.0.0 or newer. Run: geode sdk update v6.0.0 && geode sdk install-binaries")
endif()

# Keep clean builds on the same reviewed .bro files; explicit local overrides
# remain available for bindings development.
set(PAIMBNAILS_BINDINGS_REVISION "ab98d45ccb3cab07162a7e2746ea61e423632ff9")
if (NOT GEODE_BINDINGS_REPO_PATH AND NOT DEFINED ENV{GEODE_BINDINGS_REPO_PATH})
    CPMAddPackage(
        NAME bindings
        GITHUB_REPOSITORY geode-sdk/bindings
        GIT_TAG ${PAIMBNAILS_BINDINGS_REVISION}
        DOWNLOAD_ONLY YES
        NO_CACHE YES
    )
    set(GEODE_BINDINGS_REPO_PATH "${bindings_SOURCE_DIR}")
endif()

# Cached SDK dependencies must match the loader's async ABI.
set(GEODE_ARC_VERSION "@1.6.6" CACHE STRING "Geode arc version" FORCE)
set(GEODE_ASP_VERSION "@1.6.4" CACHE STRING "Geode asp version" FORCE)

option(GEODE_PACKAGE_ZSTD "Use Zstd compression for the .geode package" ON)
option(GEODE_LINK_NIGHTLY "Link the Geode nightly binaries instead of the release binaries" OFF)

# Treat Geode/cocos headers as system headers so SDK warnings stay out of
# our build; the mod's own code keeps full warnings (PAIMBNAILS_EXTRA_WARNINGS).
set(GEODE_SET_TARGET_AS_SYSTEM ON CACHE BOOL
    "Sets the target include directories for Geode as SYSTEM, silencing Geode related warning on mod build." FORCE)

add_subdirectory($ENV{GEODE_SDK} ${CMAKE_CURRENT_BINARY_DIR}/geode)

if (NOT GEODE_GD_VERSION STREQUAL "2.2091")
    message(FATAL_ERROR
        "Paimbnails targets Geometry Dash 2.2091, but the Geode SDK is configured for ${GEODE_GD_VERSION}.")
endif()

if (GEODE_PACKAGE_ZSTD)
    execute_process(
        COMMAND "${GEODE_CLI}" package new --help
        OUTPUT_VARIABLE PAIMBNAILS_PACKAGE_HELP
        RESULT_VARIABLE PAIMBNAILS_PACKAGE_HELP_RESULT
    )
    if (NOT PAIMBNAILS_PACKAGE_HELP_RESULT EQUAL 0 OR NOT PAIMBNAILS_PACKAGE_HELP MATCHES "--zstd")
        message(FATAL_ERROR "The Geode CLI must support --zstd. Update the CLI or set GEODE_PACKAGE_ZSTD=OFF.")
    endif()
endif()

include(cmake/LocalModDeploy.cmake)
include(cmake/GifComputeShaders.cmake)
if (PAIMBNAILS_DEPLOY_LOCAL_MOD)
    setup_geode_mod(${PROJECT_NAME} DONT_INSTALL)
else()
    setup_geode_mod(${PROJECT_NAME})
endif()
paimbnails_deploy_mod(${PROJECT_NAME})
target_compile_definitions(${PROJECT_NAME} PRIVATE PAIM_USE_GEODE=1)

# globed is an optional dependency; its headers are only needed for the soft
# link api, and every use of them is behind __has_include so the mod still
# builds if the dependency is not fetched.
target_include_directories(${PROJECT_NAME} SYSTEM PRIVATE
    ${CMAKE_CURRENT_BINARY_DIR}/geode-deps/dankmeme.globed2/include)

message(STATUS "Paimbnails: ${CMAKE_SYSTEM_NAME} | HW decoder: native | SW fallback: pl_mpeg")

enable_testing()
add_subdirectory(tests)
