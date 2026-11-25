# Compile DirectCompute at build time, so the first import needs neither a
# shader compiler DLL nor a CPU compilation pause. FXC ships with the Windows SDK.
if (WIN32)
    file(GLOB _paim_fxc_candidates
        "$ENV{ProgramFiles\(x86\)}/Windows Kits/10/bin/*/x64/fxc.exe")
    list(SORT _paim_fxc_candidates COMPARE NATURAL ORDER DESCENDING)
    set(_paim_fxc_hints)
    foreach(_paim_fxc IN LISTS _paim_fxc_candidates)
        get_filename_component(_paim_fxc_dir "${_paim_fxc}" DIRECTORY)
        list(APPEND _paim_fxc_hints "${_paim_fxc_dir}")
    endforeach()
    find_program(PAIMBNAILS_FXC NAMES fxc HINTS ${_paim_fxc_hints} REQUIRED)
    set(_paim_compute_dir "${CMAKE_CURRENT_BINARY_DIR}/gif-compute-shaders")
    set(_paim_compute_headers)
    foreach(_paim_kernel Coverage Quantize)
        set(_paim_shader "${PROJECT_SOURCE_DIR}/src/features/gif-import/services/shaders/Gif${_paim_kernel}.hlsl")
        set(_paim_header "${_paim_compute_dir}/Gif${_paim_kernel}Shader.hpp")
        add_custom_command(OUTPUT "${_paim_header}"
            COMMAND ${CMAKE_COMMAND} -E make_directory "${_paim_compute_dir}"
            COMMAND "${PAIMBNAILS_FXC}" /nologo /T cs_5_0 /E main /O3 /Gis
                /Vn "k${_paim_kernel}ShaderBytecode" /Fh "${_paim_header}" "${_paim_shader}"
            DEPENDS "${_paim_shader}"
            COMMENT "Compiling GIF ${_paim_kernel} GPU kernel"
            VERBATIM)
        list(APPEND _paim_compute_headers "${_paim_header}")
    endforeach()
    add_custom_target(gif-compute-shaders DEPENDS ${_paim_compute_headers})
    add_dependencies(${PROJECT_NAME} gif-compute-shaders)
    set_property(SOURCE src/features/gif-import/services/GifCompute.cpp APPEND
        PROPERTY INCLUDE_DIRECTORIES "${_paim_compute_dir}")
endif()
