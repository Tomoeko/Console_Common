foreach(test_name
    render/render_geometry
    render/material_blend
    render/image_atomic
    render/texture_source
    support/utf8
    support/regular_file
    support/ascii
    support/portable_path
    viewport)
    string(REPLACE "/" "-" target_name "cc-${test_name}")
    add_executable(${target_name}-test "${test_name}_test.c")
    target_link_libraries(${target_name}-test PRIVATE console_common)
    target_include_directories(${target_name}-test PRIVATE
        "${PROJECT_SOURCE_DIR}/src/render")
    if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
        target_compile_options(${target_name}-test PRIVATE -UNDEBUG)
    endif()
    add_test(NAME ${target_name} COMMAND ${target_name}-test)
    set_tests_properties(${target_name} PROPERTIES
        WORKING_DIRECTORY "${PROJECT_BINARY_DIR}")
endforeach()
