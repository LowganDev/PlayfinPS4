cmake_minimum_required(VERSION 3.20)

option(PLATFORM_PS4 "Build for the PS4 with the OpenOrbis toolchain" OFF)
if(PLATFORM_PS4 AND NOT CMAKE_TOOLCHAIN_FILE)
    if(NOT DEFINED ENV{OPENORBIS})
        message(FATAL_ERROR "OPENORBIS is not set")
    endif()
    set(CMAKE_TOOLCHAIN_FILE "$ENV{OPENORBIS}/cmake/ps4.cmake" CACHE FILEPATH "")
endif()

project(Playfin VERSION 1.0.1 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(PLAYFIN_TESTS "Build the unit tests and the UI test harness" ON)
option(PLAYFIN_SANITIZE "Build with AddressSanitizer and UndefinedBehaviorSanitizer" OFF)
option(PLAYFIN_FUZZ "Build the libFuzzer targets (clang)" OFF)
if(PLATFORM_PS4)
    # our copy of the toolchain's linker script (keeps .data.* and prioritised constructors, see the file)
    string(REPLACE "$ENV{OPENORBIS}/link.x" "${CMAKE_SOURCE_DIR}/scripts/ps4/link.x" CMAKE_EXE_LINKER_FLAGS
                   "${CMAKE_EXE_LINKER_FLAGS}")
    if(NOT CMAKE_EXE_LINKER_FLAGS MATCHES "scripts/ps4/link.x")
        message(FATAL_ERROR "the toolchain's linker script option was not found: ${CMAKE_EXE_LINKER_FLAGS}")
    endif()
    # the PS4 CPU (AMD Jaguar): SSE4.1 rounding instead of libm calls, AVX; every PS4 has this CPU
    add_compile_options(-march=btver2)
    set(PLAYFIN_TESTS OFF CACHE BOOL "" FORCE)
    set(PLAYFIN_FUZZ OFF CACHE BOOL "" FORCE)
endif()

include(cmake/warnings.cmake)

if(PLAYFIN_SANITIZE)
    add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=all)
    add_link_options(-fsanitize=address,undefined)
endif()
if(PLAYFIN_FUZZ)
    add_compile_options(-fsanitize=fuzzer-no-link,address,undefined -fno-omit-frame-pointer)
    add_link_options(-fsanitize=address,undefined)
endif()

find_package(PkgConfig REQUIRED)
find_package(Threads REQUIRED)
include(cmake/dependencies.cmake)

add_subdirectory(third_party)

# core utilities: no SDL, no network
add_library(playfin_base STATIC
    src/core/boot_trace.cpp
    src/core/version.cpp
    src/core/redact.cpp
    src/core/data_dir.cpp
    src/core/file_io.cpp
    src/core/log.cpp
    src/core/result.cpp
    src/core/strings.cpp
    src/core/task.cpp
    src/core/utf.cpp
)
target_include_directories(playfin_base PUBLIC src)
target_compile_definitions(playfin_base PRIVATE PLAYFIN_VERSION="${PROJECT_VERSION}")
if(PLATFORM_PS4)
    target_compile_definitions(playfin_base PUBLIC PLAYFIN_PS4=1)
endif()
target_link_libraries(playfin_base PUBLIC Threads::Threads)
playfin_set_warnings(playfin_base)

# Jellyfin client: depends on core only (+ curl, crypto, yyjson)
add_library(playfin_jellyfin STATIC
    src/net/url.cpp
    src/net/crypto.cpp
    src/jellyfin/json.cpp
    src/jellyfin/models.cpp
    src/net/http.cpp
    src/jellyfin/client.cpp
    src/jellyfin/store.cpp
    src/jellyfin/discovery.cpp
)
if(PLATFORM_PS4)
    target_sources(playfin_jellyfin PRIVATE src/net/crypto_mbedtls.cpp)
else()
    target_sources(playfin_jellyfin PRIVATE src/net/crypto_openssl.cpp)
endif()
target_link_libraries(playfin_jellyfin PUBLIC playfin_base yyjson PRIVATE playfin_curl playfin_crypto)
playfin_set_warnings(playfin_jellyfin)

# window, input, rendering helpers, system keyboard
# CPU rendering (no SDL, no GL): the NanoVG software backend and its rasterizer
add_library(playfin_render STATIC
    src/render/nvg_sw.cpp
    src/render/raster.cpp
    src/render/composite.cpp
)
target_link_libraries(playfin_render PUBLIC playfin_base nanovg)
# the per-pixel hot path: optimised in debug builds too (tests and Linux runs stay usable)
target_compile_options(playfin_render PRIVATE -O2)
playfin_set_warnings(playfin_render)

add_library(playfin_platform STATIC
    src/platform/input.cpp
    src/platform/events.cpp
    src/platform/language.cpp
    src/platform/sdl_platform.cpp
    src/render/canvas.cpp
)
if(PLATFORM_PS4)
    target_sources(playfin_platform PRIVATE src/platform/ps4/backend_ps4.cpp src/platform/ps4/firmware_shaders.cpp
        src/platform/ps4/present_ps4.cpp src/platform/ps4/text_input_ps4.cpp)
    option(PLAYFIN_GPU_PROBE "PS4: log GPU and firmware shader diagnostics at startup" OFF)
    if(PLAYFIN_GPU_PROBE)
        target_sources(playfin_platform PRIVATE src/platform/ps4/gpu_probe_ps4.cpp)
        target_compile_definitions(playfin_platform PRIVATE PLAYFIN_GPU_PROBE=1)
    endif()
else()
    target_sources(playfin_platform PRIVATE src/platform/gl_present.cpp src/platform/linux/backend_linux.cpp
        src/platform/linux/text_input_linux.cpp)
endif()
target_link_libraries(playfin_platform PUBLIC playfin_base playfin_render playfin_sdl2 nanovg stb)
playfin_set_warnings(playfin_platform)

# UI toolkit: depends on the platform layer only (never on the Jellyfin library)
add_library(playfin_ui STATIC
    src/ui/theme.cpp
    src/ui/halo.cpp
    src/ui/animation.cpp
    src/ui/text.cpp
    src/ui/i18n.cpp
    src/ui/context.cpp
    src/ui/view.cpp
    src/ui/box.cpp
    src/ui/label.cpp
    src/ui/focus.cpp
    src/ui/scroll.cpp
    src/ui/list.cpp
    src/ui/reveal.cpp
    src/ui/images.cpp
    src/ui/image_view.cpp
    src/ui/widgets.cpp
    src/ui/hints.cpp
    src/ui/application.cpp
    src/ui/nvg_backend.cpp
)
target_link_libraries(playfin_ui PUBLIC playfin_platform yyjson)
playfin_set_warnings(playfin_ui)

# video playback: libmpv with its software renderer (no UI)
add_library(playfin_player STATIC
    src/player/engine.cpp
    src/player/stream_url.cpp
    src/player/profile.cpp
    src/player/seek.cpp
    src/player/queue.cpp
    src/player/session.cpp
    src/player/tracks.cpp
)
target_link_libraries(playfin_player PUBLIC playfin_base playfin_jellyfin PRIVATE playfin_mpv)
playfin_set_warnings(playfin_player)

# app, screens and test harness
add_library(playfin_core STATIC
    src/app/app.cpp
    src/app/gallery.cpp
    src/app/settings.cpp
    src/app/shell/browse_screens.cpp
    src/app/shell/cards.cpp
    src/app/shell/chrome.cpp
    src/app/shell/connect_screens.cpp
    src/app/shell/engine_options.cpp
    src/app/shell/format.cpp
    src/app/shell/guide.cpp
    src/app/shell/library_query.cpp
    src/app/shell/licenses.cpp
    src/app/shell/livetv_screens.cpp
    src/app/shell/music_screens.cpp
    src/app/shell/music_player.cpp
    src/app/shell/nav_bar.cpp
    src/app/shell/player_screen.cpp
    src/app/shell/search_screen.cpp
    src/app/shell/settings_screen.cpp
    src/app/shell/session.cpp
)
target_link_libraries(playfin_core PUBLIC playfin_ui playfin_jellyfin playfin_player)
playfin_set_warnings(playfin_core)

add_executable(playfin src/main.cpp)
target_link_libraries(playfin PRIVATE playfin_core)
playfin_set_warnings(playfin)
if(PLATFORM_PS4)
    # in the executable itself (not an archive): it must win over libkernel's clock_gettime
    target_sources(playfin PRIVATE src/platform/ps4/clock_ps4.cpp src/platform/ps4/socket_ps4.cpp)
    # every socket()/socketpair() call of the executable (curl included) goes through socket_ps4.cpp
    target_link_options(playfin PRIVATE --wrap=socket --wrap=socketpair)
    include(cmake/ps4_pkg.cmake)
    playfin_add_ps4_pkg(playfin)
endif()
if(NOT PLATFORM_PS4)
    target_sources(playfin_core PRIVATE src/testing/script.cpp src/testing/image.cpp)
    target_compile_definitions(playfin_core PUBLIC PLAYFIN_TESTING=1)
    add_executable(playfin_imgdiff src/testing/imgdiff_main.cpp)
    target_link_libraries(playfin_imgdiff PRIVATE playfin_core)
    playfin_set_warnings(playfin_imgdiff)
    # every build (not only when playfin relinks): an edited string or licence reaches the tests
    add_custom_target(playfin_resources ALL
        COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/resources/fonts $<TARGET_FILE_DIR:playfin>/resources/fonts
        COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/resources/certs $<TARGET_FILE_DIR:playfin>/resources/certs
        COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/resources/i18n $<TARGET_FILE_DIR:playfin>/resources/i18n
        COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/resources/licenses
            $<TARGET_FILE_DIR:playfin>/resources/licenses
        COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_SOURCE_DIR}/THIRD_PARTY_LICENSES $<TARGET_FILE_DIR:playfin>/resources/)
    add_dependencies(playfin playfin_resources)
endif()

if(PLAYFIN_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

if(PLAYFIN_FUZZ)
    enable_testing()
    foreach(name models discovery url redact raster image)
        add_executable(fuzz_${name} fuzz/fuzz_${name}.cpp)
        target_link_libraries(fuzz_${name} PRIVATE playfin_jellyfin playfin_render playfin_ui)
        target_link_options(fuzz_${name} PRIVATE -fsanitize=fuzzer)
        playfin_set_warnings(fuzz_${name})
        # first directory: writable corpus in the build tree; second: the committed seeds (read only)
        file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/corpus/${name} ${CMAKE_BINARY_DIR}/fuzz-artifacts)
        add_test(NAME fuzz_${name}
            COMMAND fuzz_${name} -max_total_time=30 -rss_limit_mb=2048
                -artifact_prefix=${CMAKE_BINARY_DIR}/fuzz-artifacts/${name}-
                ${CMAKE_BINARY_DIR}/corpus/${name} ${CMAKE_SOURCE_DIR}/fuzz/seeds/${name})
        set_tests_properties(fuzz_${name} PROPERTIES LABELS fuzz TIMEOUT 120)
    endforeach()
endif()
