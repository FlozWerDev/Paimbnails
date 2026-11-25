find_package(Python3 COMPONENTS Interpreter QUIET)

if (Python3_Interpreter_FOUND)
    set(PAIMBNAILS_REGRESSION_COMMAND
        ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/run_regressions.py
        --suite portable --build-dir ${CMAKE_BINARY_DIR}
        --cxx ${CMAKE_CXX_COMPILER})
    add_custom_target(check
        COMMAND ${PAIMBNAILS_REGRESSION_COMMAND}
        WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
        USES_TERMINAL VERBATIM)
    add_test(NAME paimbnails-portable-regressions COMMAND ${PAIMBNAILS_REGRESSION_COMMAND})
    if (WIN32 AND CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        add_custom_target(check-gif-compute
            COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/run_gif_compute_regression.py
                --build-dir ${CMAKE_BINARY_DIR} --cxx ${CMAKE_CXX_COMPILER}
            WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
            USES_TERMINAL VERBATIM)
    endif()
    if (DEFINED ENV{GEODE_SDK} AND NOT "$ENV{GEODE_SDK}" STREQUAL "")
        add_custom_target(check-bindings
            COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/run_regressions.py
                --suite bindings --build-dir ${CMAKE_BINARY_DIR}
                --cxx ${CMAKE_CXX_COMPILER} --sdk $ENV{GEODE_SDK}
            WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
            USES_TERMINAL VERBATIM)
    endif()
else()
    message(WARNING "Python3 interpreter not found: paimbnails-portable-regressions will be skipped. Set Python3_EXECUTABLE to run the portable regression suite.")
    add_test(NAME paimbnails-portable-regressions
        COMMAND ${CMAKE_COMMAND} -E echo "Python3 interpreter not found: portable regression suite skipped.")
endif()
