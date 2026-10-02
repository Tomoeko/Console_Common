foreach(test_name
    render/clip
    render/render_geometry
    render/material
    render/material_blend
    render/image_atomic
    render/texture_source
    resources/gx_texture
    support/endian
    support/affine
    support/interpolation
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

if(APPLE)
    enable_language(OBJC)
    add_executable(cc-metal-pipeline-test platform/metal_pipeline_test.m)
    cc_add_platform(cc-metal-pipeline-test metal)
    target_include_directories(cc-metal-pipeline-test PRIVATE
        "${PROJECT_SOURCE_DIR}/src/platform/metal")
    set_source_files_properties(platform/metal_pipeline_test.m
        PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
    target_compile_options(cc-metal-pipeline-test PRIVATE -UNDEBUG)
    add_test(NAME cc-metal-pipeline COMMAND cc-metal-pipeline-test)
    set_tests_properties(cc-metal-pipeline PROPERTIES SKIP_RETURN_CODE 77)
endif()

# Texture metadata is independent of a graphics context and is tested on every host.
add_executable(cc-texture-dimensions-test platform/texture_dimensions_test.c
    "${PROJECT_SOURCE_DIR}/src/platform/gles2/texture_dimensions.c")
target_include_directories(cc-texture-dimensions-test PRIVATE
    "${PROJECT_SOURCE_DIR}/src/platform/gles2")
target_compile_features(cc-texture-dimensions-test PRIVATE c_std_11)
if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(cc-texture-dimensions-test PRIVATE
        -Wall -Wextra -Wpedantic -Wconversion -UNDEBUG)
endif()
add_test(NAME cc-texture-dimensions COMMAND cc-texture-dimensions-test)
