file(GLOB PLAYFIN_UNIT_TESTS CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/unit/*_test.cpp)
add_executable(playfin_tests unit/main.cpp ${PLAYFIN_UNIT_TESTS})
target_link_libraries(playfin_tests PRIVATE playfin_core doctest)
target_include_directories(playfin_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(playfin_tests PRIVATE PLAYFIN_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
    PLAYFIN_PROJECT_VERSION="${PROJECT_VERSION}")
playfin_set_warnings(playfin_tests)
add_library(playfin_test_support STATIC support/fake_server.cpp support/fake_jellyfin.cpp)
target_link_libraries(playfin_test_support PUBLIC playfin_jellyfin httplib stb)
target_include_directories(playfin_test_support PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(playfin_test_support PUBLIC PLAYFIN_TEST_FIXTURES="${CMAKE_CURRENT_SOURCE_DIR}/fixtures")
playfin_set_warnings(playfin_test_support)
target_link_libraries(playfin_tests PRIVATE playfin_test_support stb)
add_test(NAME unit COMMAND playfin_tests)

add_executable(fake_jellyfin support/fake_jellyfin_main.cpp)
target_link_libraries(fake_jellyfin PRIVATE playfin_test_support)
playfin_set_warnings(fake_jellyfin)

set(PLAYFIN_HEADLESS_ENV "SDL_VIDEODRIVER=offscreen;MESA_LOADER_DRIVER_OVERRIDE=llvmpipe")
add_test(NAME app_smoke COMMAND playfin --hidden --no-discovery --frames 5 --data-dir ${CMAKE_BINARY_DIR}/test-data/smoke)
set_tests_properties(app_smoke PROPERTIES ENVIRONMENT "${PLAYFIN_HEADLESS_ENV}" TIMEOUT 60)

add_test(NAME ui_gallery
    COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/ui/run.sh $<TARGET_FILE:playfin> $<TARGET_FILE:playfin_imgdiff>
        ${CMAKE_CURRENT_SOURCE_DIR}/ui/gallery.script ${CMAKE_CURRENT_SOURCE_DIR}/ui/references/gallery
        ${CMAKE_BINARY_DIR}/ui-out/gallery --screen gallery)
set_tests_properties(ui_gallery PROPERTIES TIMEOUT 180)

# the same script through NanoVG's GLES2 backend: the references are its output, the software renderer must match
add_test(NAME ui_gallery_gl
    COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/ui/run.sh $<TARGET_FILE:playfin> $<TARGET_FILE:playfin_imgdiff>
        ${CMAKE_CURRENT_SOURCE_DIR}/ui/gallery.script ${CMAKE_CURRENT_SOURCE_DIR}/ui/references/gallery
        ${CMAKE_BINARY_DIR}/ui-out/gallery-gl --screen gallery --renderer gl)
set_tests_properties(ui_gallery_gl PROPERTIES TIMEOUT 180)

add_test(NAME ui_shell
    COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/ui/run_shell.sh $<TARGET_FILE:playfin> $<TARGET_FILE:playfin_imgdiff>
        $<TARGET_FILE:fake_jellyfin> ${CMAKE_CURRENT_SOURCE_DIR}/ui/shell.script
        ${CMAKE_CURRENT_SOURCE_DIR}/ui/references/shell ${CMAKE_BINARY_DIR}/ui-out/shell)
set_tests_properties(ui_shell PROPERTIES TIMEOUT 180 RUN_SERIAL TRUE)

# music and Live TV on the fake server with the test server's views, the guide's clock fixed (Paris, UTC+2)
add_test(NAME ui_media
    COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/ui/run_shell.sh $<TARGET_FILE:playfin> $<TARGET_FILE:playfin_imgdiff>
        $<TARGET_FILE:fake_jellyfin> ${CMAKE_CURRENT_SOURCE_DIR}/ui/media.script
        ${CMAKE_CURRENT_SOURCE_DIR}/ui/references/media ${CMAKE_BINARY_DIR}/ui-out/media
        "--media" "--fixed-time 2026-10-16T19:45:00Z --utc-offset 120")
set_tests_properties(ui_media PROPERTIES TIMEOUT 180 RUN_SERIAL TRUE)

# no usable data folder: the app still starts (nothing is saved) instead of quitting
add_test(NAME app_without_data_folder
    COMMAND playfin --hidden --no-discovery --frames 5 --data-dir /dev/null/playfin)
set_tests_properties(app_without_data_folder PROPERTIES ENVIRONMENT "${PLAYFIN_HEADLESS_ENV}" TIMEOUT 60)

# a test script without an output folder would write its screenshots to "/": refused before anything starts
add_test(NAME cli_test_output_required
    COMMAND playfin --hidden --no-discovery --test-script ${CMAKE_CURRENT_SOURCE_DIR}/ui/gallery.script
        --data-dir ${CMAKE_BINARY_DIR}/test-data/cli)
set_tests_properties(cli_test_output_required PROPERTIES ENVIRONMENT "${PLAYFIN_HEADLESS_ENV}" TIMEOUT 30
    PASS_REGULAR_EXPRESSION "--test-script needs --test-output")

add_test(NAME init_failure_logged
    COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/cli/init_failure_logged.sh $<TARGET_FILE:playfin>
        ${CMAKE_BINARY_DIR}/test-data/init-failure)
set_tests_properties(init_failure_logged PROPERTIES TIMEOUT 60)
