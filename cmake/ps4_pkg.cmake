# PS4 packaging with the OpenOrbis tools: ELF -> eboot.bin (create-fself) -> param.sfo -> pkg.gp4 -> pkg.
# Shipped from the toolchain samples: sce_sys/about/right.sprx and sce_module/{libc,libSceFios2}.prx.
function(playfin_add_ps4_pkg target)
    set(tools "$ENV{OPENORBIS}/bin/linux")
    set(samples "$ENV{OPENORBIS}/samples/piglet")
    set(stage "${CMAKE_BINARY_DIR}/pkg")
    set(title_id "PLFN00000")
    set(auth_info "000000000000000000000000001C004000FF000000000080000000000000000000000000000000000000008000400040000000000000008000000000000000080040FFFF000000F000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000")
    set(content_id "IV0001-PLFN00000_00-PLFN000000100000")
    # param.sfo versions are "XX.YY"
    set(major "0${PROJECT_VERSION_MAJOR}")
    set(minor "0${PROJECT_VERSION_MINOR}")
    string(LENGTH "${major}" length)
    math(EXPR start "${length} - 2")
    string(SUBSTRING "${major}" ${start} 2 major)
    string(LENGTH "${minor}" length)
    math(EXPR start "${length} - 2")
    string(SUBSTRING "${minor}" ${start} 2 minor)
    set(version "${major}.${minor}")
    set(sfo "${stage}/sce_sys/param.sfo")
    set(pkg "${stage}/${content_id}.pkg")
    set(files
        eboot.bin
        sce_sys/param.sfo
        sce_sys/icon0.png
        sce_sys/pic1.png
        sce_sys/about/right.sprx
        sce_module/libc.prx
        sce_module/libSceFios2.prx
    )
    file(GLOB_RECURSE resource_files RELATIVE ${CMAKE_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/resources/fonts/* ${CMAKE_SOURCE_DIR}/resources/i18n/* ${CMAKE_SOURCE_DIR}/resources/certs/*
        ${CMAKE_SOURCE_DIR}/resources/licenses/*)
    list(APPEND files ${resource_files} resources/THIRD_PARTY_LICENSES)
    string(REPLACE ";" " " files_arg "${files}")
    file(MAKE_DIRECTORY "${stage}")

    add_custom_command(OUTPUT "${pkg}"
        COMMAND ${CMAKE_COMMAND} -E rm -rf sce_sys sce_module resources eboot.bin pkg.gp4 "${pkg}"
        COMMAND ${CMAKE_COMMAND} -E make_directory sce_sys/about sce_module resources resources/licenses
        # program authority id and capabilities: the toolchain's defaults for homebrew apps. Without the auth info
        # the app may not map the system flexible memory that the C library's heap (musl, 2600 MiB) is built on:
        # the first malloc crashed on the console (CE-34878-0)
        COMMAND "${tools}/create-fself" "-in=$<TARGET_FILE:${target}>" "-out=${CMAKE_BINARY_DIR}/${target}.oelf"
            --eboot "eboot.bin" --paid 0x3800000000000035 --authinfo "${auth_info}"
        COMMAND ${CMAKE_COMMAND} -E copy "${CMAKE_SOURCE_DIR}/resources/ps4/sce_sys/icon0.png"
            "${CMAKE_SOURCE_DIR}/resources/ps4/sce_sys/pic1.png" sce_sys/
        COMMAND ${CMAKE_COMMAND} -E copy "${samples}/sce_sys/about/right.sprx" sce_sys/about/right.sprx
        COMMAND ${CMAKE_COMMAND} -E copy "${samples}/sce_module/libc.prx" "${samples}/sce_module/libSceFios2.prx" sce_module/
        COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_SOURCE_DIR}/resources/fonts" resources/fonts
        COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_SOURCE_DIR}/resources/certs" resources/certs
        COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_SOURCE_DIR}/resources/i18n" resources/i18n
        COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_SOURCE_DIR}/resources/licenses" resources/licenses
        COMMAND ${CMAKE_COMMAND} -E copy "${CMAKE_SOURCE_DIR}/THIRD_PARTY_LICENSES" resources/
        COMMAND "${tools}/PkgTool.Core" sfo_new "${sfo}"
        COMMAND "${tools}/PkgTool.Core" sfo_setentry "${sfo}" APP_TYPE --type Integer --maxsize 4 --value 1
        COMMAND "${tools}/PkgTool.Core" sfo_setentry "${sfo}" APP_VER --type Utf8 --maxsize 8 --value "${version}"
        # 0x2: system dialogs follow the console "Enter button" setting
        COMMAND "${tools}/PkgTool.Core" sfo_setentry "${sfo}" ATTRIBUTE --type Integer --maxsize 4 --value 2
        COMMAND "${tools}/PkgTool.Core" sfo_setentry "${sfo}" CATEGORY --type Utf8 --maxsize 4 --value gde
        COMMAND "${tools}/PkgTool.Core" sfo_setentry "${sfo}" CONTENT_ID --type Utf8 --maxsize 48 --value "${content_id}"
        COMMAND "${tools}/PkgTool.Core" sfo_setentry "${sfo}" FORMAT --type Utf8 --maxsize 4 --value obs
        COMMAND "${tools}/PkgTool.Core" sfo_setentry "${sfo}" DOWNLOAD_DATA_SIZE --type Integer --maxsize 4 --value 0
        COMMAND "${tools}/PkgTool.Core" sfo_setentry "${sfo}" SYSTEM_VER --type Integer --maxsize 4 --value 1020
        COMMAND "${tools}/PkgTool.Core" sfo_setentry "${sfo}" TITLE --type Utf8 --maxsize 128 --value Playfin
        COMMAND "${tools}/PkgTool.Core" sfo_setentry "${sfo}" TITLE_ID --type Utf8 --maxsize 12 --value "${title_id}"
        COMMAND "${tools}/PkgTool.Core" sfo_setentry "${sfo}" VERSION --type Utf8 --maxsize 8 --value "${version}"
        COMMAND "${tools}/create-gp4" -out pkg.gp4 "--content-id=${content_id}" --files "${files_arg}"
        COMMAND "${tools}/PkgTool.Core" pkg_build pkg.gp4 .
        WORKING_DIRECTORY "${stage}"
        DEPENDS ${target} "${CMAKE_SOURCE_DIR}/resources/ps4/sce_sys/icon0.png"
            "${CMAKE_SOURCE_DIR}/resources/ps4/sce_sys/pic1.png"
        VERBATIM)
    add_custom_target(${target}_pkg ALL DEPENDS "${pkg}")
endfunction()
