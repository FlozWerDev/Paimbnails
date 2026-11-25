# Keep a local copy of every completed package in the requested game installation.
set(_paimbnails_default_mods "C:/Program Files (x86)/Steam/steamapps/common/Geometry Dash/geode/mods")
# Existing build directories retain CACHE values when the default changes.
# Migrate the old installation only; preserve any other explicit destination.
if (WIN32 AND DEFINED PAIMBNAILS_MODS_DIRECTORY)
    string(REPLACE "\\" "/" _paimbnails_previous_mods "${PAIMBNAILS_MODS_DIRECTORY}")
    if (_paimbnails_previous_mods STREQUAL "F:/SteamLibrary/steamapps/common/Geometry Dash/geode/mods")
        set(PAIMBNAILS_MODS_DIRECTORY "${_paimbnails_default_mods}"
            CACHE PATH "Destination for locally built .geode packages" FORCE)
    endif()
    unset(_paimbnails_previous_mods)
endif()
set(PAIMBNAILS_MODS_DIRECTORY "${_paimbnails_default_mods}"
    CACHE PATH "Destination for locally built .geode packages")
unset(_paimbnails_default_mods)
set(_paimbnails_deploy_default OFF)
# The build creates the destination, including on the first local installation.
if (WIN32 AND NOT DEFINED ENV{CI})
    set(_paimbnails_deploy_default ON)
endif()
option(PAIMBNAILS_DEPLOY_LOCAL_MOD "Copy completed packages into the local Geode mods directory"
    ${_paimbnails_deploy_default})
unset(_paimbnails_deploy_default)

function(paimbnails_deploy_mod target)
    if (NOT PAIMBNAILS_DEPLOY_LOCAL_MOD OR NOT TARGET ${target}_PACKAGE)
        return()
    endif()
    file(READ "${PROJECT_SOURCE_DIR}/mod.json" _manifest)
    string(JSON _mod_id GET "${_manifest}" id)
    set(_deploy_commands
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${PAIMBNAILS_MODS_DIRECTORY}"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${CMAKE_CURRENT_BINARY_DIR}/${_mod_id}.geode"
            "${PAIMBNAILS_MODS_DIRECTORY}/${_mod_id}.geode")
    add_custom_command(TARGET ${target}_PACKAGE POST_BUILD
        ${_deploy_commands}
        COMMENT "Installing ${_mod_id}.geode in ${PAIMBNAILS_MODS_DIRECTORY}"
        VERBATIM)

    # Building the library explicitly skips Geode's separate PACKAGE target.
    # Package the freshly linked binary before copying so this path cannot install an old build.
    if (TARGET ${target} AND GEODE_CLI AND NOT GEODE_CLI STREQUAL "GEODE_CLI-NOTFOUND")
        get_target_property(_target_type ${target} TYPE)
        if (_target_type STREQUAL "SHARED_LIBRARY" OR _target_type STREQUAL "MODULE_LIBRARY")
            set(_package_flags)
            if (GEODE_PACKAGE_ZSTD)
                list(APPEND _package_flags --zstd)
            endif()
            add_custom_command(TARGET ${target} POST_BUILD
                COMMAND "${GEODE_CLI}" package new "${PROJECT_SOURCE_DIR}"
                    --binary "$<TARGET_FILE:${target}>"
                    --output "${CMAKE_CURRENT_BINARY_DIR}/${_mod_id}.geode"
                    ${_package_flags}
                ${_deploy_commands}
                COMMENT "Packaging and installing ${_mod_id}.geode after linking"
                VERBATIM)
        endif()
    endif()
    message(STATUS "Paimbnails packages will be copied to: ${PAIMBNAILS_MODS_DIRECTORY}")
endfunction()
