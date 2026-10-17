add_library(doctest INTERFACE)
target_include_directories(doctest SYSTEM INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})

add_library(nanovg STATIC nanovg/nanovg.c nanovg_gles2.c)
target_include_directories(nanovg SYSTEM PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/nanovg ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(nanovg PUBLIC NANOVG_GLES2)
# stb_image (implemented in nanovg.c) decodes pictures from the server: only JPEG and PNG (what Jellyfin sends),
# and pictures larger than ui::kMaxImageSide are refused inside the decoder too
target_compile_definitions(nanovg PRIVATE STBI_ONLY_JPEG STBI_ONLY_PNG STBI_MAX_DIMENSIONS=4096)
if(NOT PLATFORM_PS4)
    pkg_check_modules(GLESV2 REQUIRED IMPORTED_TARGET glesv2)
    target_link_libraries(nanovg PUBLIC PkgConfig::GLESV2 m)
endif()
if(PLAYFIN_SANITIZE OR PLAYFIN_FUZZ)
    # nanovg.c holds stb_image (upstream, harmless): its JPEG decoder shifts negative values, and an empty PNG data
    # chunk copies zero bytes from a null buffer
    set_source_files_properties(nanovg/nanovg.c PROPERTIES COMPILE_OPTIONS
        "-fno-sanitize=shift;-fno-sanitize=pointer-overflow;-fno-sanitize=nonnull-attribute")
endif()

add_library(stb STATIC stb_impl.c)
target_include_directories(stb SYSTEM PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
if(PLAYFIN_SANITIZE OR PLAYFIN_FUZZ)
    # stb_image_write's JPEG bit buffer shifts into the sign bit (upstream, harmless): not our code to fix
    target_compile_options(stb PRIVATE -fno-sanitize=shift)
endif()

add_library(yyjson STATIC yyjson/yyjson.c)
target_include_directories(yyjson SYSTEM PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/yyjson)

if(NOT PLATFORM_PS4)
    # test-only HTTP(S) server
    pkg_check_modules(OPENSSL REQUIRED IMPORTED_TARGET openssl)
    add_library(httplib INTERFACE)
    target_include_directories(httplib SYSTEM INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/httplib)
    target_compile_definitions(httplib INTERFACE CPPHTTPLIB_OPENSSL_SUPPORT)
    target_link_libraries(httplib INTERFACE PkgConfig::OPENSSL)
endif()
