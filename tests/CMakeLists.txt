find_package(Threads REQUIRED)
get_target_property(common_root console_common SOURCE_DIR)
if(MSVC)
    add_compile_options(/UNDEBUG)
endif()

if(WIN32)
    foreach(module window audio_order audio_output tool_io)
        add_executable(cc-windows-${module}-test platform/windows/${module}_test.c)
        target_include_directories(cc-windows-${module}-test PRIVATE "${common_root}/src")
        target_link_libraries(cc-windows-${module}-test PRIVATE console_common
            user32 gdi32 winmm avrt)
        add_test(NAME cc-windows-${module} COMMAND cc-windows-${module}-test)
        set_tests_properties(cc-windows-${module} PROPERTIES TIMEOUT 15)
    endforeach()
endif()

add_executable(cc-process-test support/process_test.c)
target_link_libraries(cc-process-test PRIVATE console_common)
if(NOT MSVC)
    target_compile_options(cc-process-test PRIVATE -UNDEBUG)
endif()
file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/tests/process with spaces")
add_test(NAME cc-process COMMAND cc-process-test
    "${CMAKE_BINARY_DIR}/tests/process with spaces")
set_tests_properties(cc-process PROPERTIES TIMEOUT 10)

find_package(Python3 QUIET COMPONENTS Interpreter)
if(Python3_Interpreter_FOUND)
    add_test(NAME cc-build-changes COMMAND "${Python3_EXECUTABLE}"
        "${common_root}/tests/support/build_changes_test.py")
    set_tests_properties(cc-build-changes PROPERTIES
        ENVIRONMENT "PYTHONDONTWRITEBYTECODE=1")
endif()

add_executable(cc-thread-test support/thread_test.c)
target_link_libraries(cc-thread-test PRIVATE console_common Threads::Threads)
if(NOT MSVC)
    target_compile_options(cc-thread-test PRIVATE -UNDEBUG)
endif()
add_test(NAME cc-thread COMMAND cc-thread-test)
set_tests_properties(cc-thread PROPERTIES TIMEOUT 10)

add_executable(cc-directory-test support/directory_test.c)
target_include_directories(cc-directory-test PRIVATE "${common_root}/src")
target_link_libraries(cc-directory-test PRIVATE console_common)
if(NOT MSVC)
    target_compile_options(cc-directory-test PRIVATE -UNDEBUG)
endif()
file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/tests/directory")
add_test(NAME cc-directory COMMAND cc-directory-test
    "${CMAKE_BINARY_DIR}/tests/directory")

add_executable(cc-display-settings-test support/display_settings_test.c)
target_include_directories(cc-display-settings-test PRIVATE "${common_root}/src")
target_link_libraries(cc-display-settings-test PRIVATE console_common)
if(NOT MSVC)
    target_compile_options(cc-display-settings-test PRIVATE -UNDEBUG)
endif()
file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/tests/display-settings")
add_test(NAME cc-display-settings COMMAND cc-display-settings-test
    "${CMAKE_BINARY_DIR}/tests/display-settings")

add_executable(cc-audio-resampler-test audio/resampler_test.c)
target_link_libraries(cc-audio-resampler-test PRIVATE console_common_audio)
target_compile_features(cc-audio-resampler-test PRIVATE c_std_11)
if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(cc-audio-resampler-test PRIVATE
        -Wall -Wextra -Wpedantic -Wconversion -UNDEBUG)
endif()
add_test(NAME cc-audio-resampler COMMAND cc-audio-resampler-test)

if(APPLE)
    add_executable(cc-audio-output-apple-test audio/output_apple_test.c)
    target_include_directories(cc-audio-output-apple-test PRIVATE
        "${common_root}/include" "${common_root}/src")
    target_link_libraries(cc-audio-output-apple-test PRIVATE Threads::Threads)
    target_compile_features(cc-audio-output-apple-test PRIVATE c_std_11)
    if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
        target_compile_options(cc-audio-output-apple-test PRIVATE
            -Wall -Wextra -Wpedantic -Wconversion -UNDEBUG)
    endif()
    add_test(NAME cc-audio-output-apple COMMAND cc-audio-output-apple-test)
endif()

if(APPLE)
    add_executable(cc-capture-audio-apple-test capture/capture_audio_apple_test.c)
    target_link_libraries(cc-capture-audio-apple-test PRIVATE console_common_capture
        "-framework CoreFoundation")
    target_include_directories(cc-capture-audio-apple-test PRIVATE "${common_root}/src")
    if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
        target_compile_options(cc-capture-audio-apple-test PRIVATE
            -Wall -Wextra -Wpedantic -Wconversion -UNDEBUG)
    endif()
    set(audio_test_directory "${CMAKE_BINARY_DIR}/tests/capture-audio-apple")
    file(MAKE_DIRECTORY "${audio_test_directory}")
    add_test(NAME cc-capture-audio-apple
        COMMAND cc-capture-audio-apple-test "${audio_test_directory}")
    set_tests_properties(cc-capture-audio-apple PROPERTIES
        SKIP_RETURN_CODE 77 FIXTURES_SETUP capture-web-fixtures)
    if(CC_USE_SYSTEM_VIDEO_ENCODER)
        enable_language(OBJC)
        add_executable(cc-capture-player-apple-test capture/capture_player_apple_test.m)
        target_link_libraries(cc-capture-player-apple-test PRIVATE
            "-framework AVFoundation" "-framework CoreMedia" "-framework Foundation")
        if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
            target_compile_options(cc-capture-player-apple-test PRIVATE
                -Wall -Wextra -Wpedantic -Wconversion -UNDEBUG
                -Wno-deprecated-declarations -fobjc-arc)
        endif()
        add_test(NAME cc-capture-player-apple
            COMMAND cc-capture-player-apple-test "${audio_test_directory}")
        set_tests_properties(cc-capture-player-apple PROPERTIES
            FIXTURES_REQUIRED capture-web-fixtures)
    endif()
endif()

# The Linux route is mocked on Unix, including Apple hosts without ALSA.
if(UNIX)
    add_executable(cc-audio-output-linux-test audio/output_linux_test.c)
    target_include_directories(cc-audio-output-linux-test PRIVATE
        "${common_root}/include" "${common_root}/src")
    target_link_libraries(cc-audio-output-linux-test PRIVATE Threads::Threads)
    target_compile_features(cc-audio-output-linux-test PRIVATE c_std_11)
    if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
        target_compile_options(cc-audio-output-linux-test PRIVATE
            -Wall -Wextra -Wpedantic -Wconversion -UNDEBUG)
    endif()
    add_test(NAME cc-audio-output-linux COMMAND cc-audio-output-linux-test)
endif()

foreach(test_name
    audio/description
    platform/framebuffer
    platform/window_controls
    render/clip
    render/render_geometry
    render/material
    render/material_blend
    render/image_atomic
    render/indexed
    render/indexed_strip
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
    if(WIN32 AND test_name MATCHES "^(render/(image_atomic|texture_source)|support/regular_file)$")
        continue()
    endif()
    string(REPLACE "/" "-" target_name "cc-${test_name}")
    add_executable(${target_name}-test "${test_name}_test.c")
    target_link_libraries(${target_name}-test PRIVATE console_common)
    target_include_directories(${target_name}-test PRIVATE
        "${common_root}/src/render"
        "${common_root}/src/platform")
    if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
        target_compile_options(${target_name}-test PRIVATE -UNDEBUG)
    endif()
    add_test(NAME ${target_name} COMMAND ${target_name}-test)
    set_tests_properties(${target_name} PROPERTIES
        WORKING_DIRECTORY "${PROJECT_BINARY_DIR}")
endforeach()

add_executable(cc-audio-test platform/audio_test.c)
target_link_libraries(cc-audio-test PRIVATE console_common_audio)
if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(cc-audio-test PRIVATE
        -Wall -Wextra -Wpedantic -Wconversion)
endif()
add_test(NAME cc-audio COMMAND cc-audio-test)
set_tests_properties(cc-audio PROPERTIES SKIP_RETURN_CODE 77)

if(APPLE)
    enable_language(OBJC)
    add_executable(cc-metal-pipeline-test platform/metal_pipeline_test.m)
    cc_add_platform(cc-metal-pipeline-test metal)
    target_include_directories(cc-metal-pipeline-test PRIVATE
        "${common_root}/src/platform/metal")
    set_source_files_properties(platform/metal_pipeline_test.m
        PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
    target_compile_options(cc-metal-pipeline-test PRIVATE -UNDEBUG)
    add_test(NAME cc-metal-pipeline COMMAND cc-metal-pipeline-test)
    set_tests_properties(cc-metal-pipeline PROPERTIES SKIP_RETURN_CODE 77)
    add_executable(cc-indexed-metal-test platform/indexed_metal_test.m)
    cc_add_platform(cc-indexed-metal-test metal)
    set_source_files_properties(platform/indexed_metal_test.m
        PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
    add_test(NAME cc-indexed-metal COMMAND cc-indexed-metal-test)
    set_tests_properties(cc-indexed-metal PROPERTIES SKIP_RETURN_CODE 77)
    add_executable(cc-indexed-pass-metal-test platform/indexed_pass_metal_test.m)
    cc_add_platform(cc-indexed-pass-metal-test metal)
    set_source_files_properties(platform/indexed_pass_metal_test.m
        PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
    target_compile_options(cc-indexed-pass-metal-test PRIVATE
        -Wall -Wextra -Wpedantic -Wconversion)
    add_test(NAME cc-indexed-pass-metal COMMAND cc-indexed-pass-metal-test)
    set_tests_properties(cc-indexed-pass-metal PROPERTIES SKIP_RETURN_CODE 77)

    add_executable(cc-indexed-pass-gles2-apple-test
        platform/indexed_pass_gles2_apple_test.c
        "${common_root}/src/platform/gles2/indexed_gles2.c")
    target_include_directories(cc-indexed-pass-gles2-apple-test PRIVATE
        "${common_root}/tests/platform/apple_gl_compat"
        "${common_root}/src/platform/gles2"
        "${common_root}/src/render")
    target_compile_options(cc-indexed-pass-gles2-apple-test PRIVATE
        -Wall -Wextra -Wpedantic -Wconversion -Wno-deprecated-declarations
        -include "${common_root}/tests/platform/apple_gl_compat/indexed_host.h")
    target_link_libraries(cc-indexed-pass-gles2-apple-test PRIVATE
        console_common "-framework OpenGL")
    add_test(NAME cc-indexed-pass-gles2-apple COMMAND cc-indexed-pass-gles2-apple-test)
    set_tests_properties(cc-indexed-pass-gles2-apple PROPERTIES SKIP_RETURN_CODE 77)

    add_executable(cc-indexed-texture-metal-test platform/indexed_texture_metal_test.m)
    cc_add_platform(cc-indexed-texture-metal-test metal)
    set_source_files_properties(platform/indexed_texture_metal_test.m
        PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
    target_compile_options(cc-indexed-texture-metal-test PRIVATE
        -Wall -Wextra -Werror -Wpedantic -Wconversion)
    add_test(NAME cc-indexed-texture-metal COMMAND cc-indexed-texture-metal-test)
    set_tests_properties(cc-indexed-texture-metal PROPERTIES SKIP_RETURN_CODE 77)

    add_executable(cc-indexed-texture-partial-metal-test
        platform/indexed_texture_partial_metal_test.c)
    cc_add_platform(cc-indexed-texture-partial-metal-test metal)
    target_compile_options(cc-indexed-texture-partial-metal-test PRIVATE
        -Wall -Wextra -Werror -Wpedantic -Wconversion)
    add_test(NAME cc-indexed-texture-partial-metal
        COMMAND cc-indexed-texture-partial-metal-test)

    add_executable(cc-indexed-texture-gles2-apple-test
        platform/indexed_texture_gles2_apple_test.c
        "${common_root}/src/platform/gles2/indexed_gles2.c")
    target_include_directories(cc-indexed-texture-gles2-apple-test PRIVATE
        "${common_root}/tests/platform/apple_gl_compat"
        "${common_root}/src/platform/gles2"
        "${common_root}/src/render")
    target_compile_options(cc-indexed-texture-gles2-apple-test PRIVATE
        -Wall -Wextra -Werror -Wpedantic -Wconversion -Wno-deprecated-declarations
        -include "${common_root}/tests/platform/apple_gl_compat/indexed_host.h")
    target_link_libraries(cc-indexed-texture-gles2-apple-test PRIVATE
        console_common "-framework OpenGL")
    add_test(NAME cc-indexed-texture-gles2-apple
        COMMAND cc-indexed-texture-gles2-apple-test)
    set_tests_properties(cc-indexed-texture-gles2-apple PROPERTIES SKIP_RETURN_CODE 77)

    add_executable(cc-indexed-mesh-metal-test platform/indexed_mesh_metal_test.m)
    cc_add_platform(cc-indexed-mesh-metal-test metal)
    set_source_files_properties(platform/indexed_mesh_metal_test.m
        PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
    target_compile_options(cc-indexed-mesh-metal-test PRIVATE
        -Wall -Wextra -Werror -Wpedantic -Wconversion)
    add_test(NAME cc-indexed-mesh-metal COMMAND cc-indexed-mesh-metal-test)
    set_tests_properties(cc-indexed-mesh-metal PROPERTIES SKIP_RETURN_CODE 77)

    add_executable(cc-indexed-mesh-gles2-apple-test
        platform/indexed_mesh_gles2_apple_test.c
        "${common_root}/src/platform/gles2/indexed_gles2.c")
    target_include_directories(cc-indexed-mesh-gles2-apple-test PRIVATE
        "${common_root}/tests/platform/apple_gl_compat"
        "${common_root}/src/platform/gles2"
        "${common_root}/src/render")
    target_compile_options(cc-indexed-mesh-gles2-apple-test PRIVATE
        -Wall -Wextra -Werror -Wpedantic -Wconversion -Wno-deprecated-declarations
        -include "${common_root}/tests/platform/apple_gl_compat/indexed_host.h")
    target_link_libraries(cc-indexed-mesh-gles2-apple-test PRIVATE
        console_common "-framework OpenGL")
    add_test(NAME cc-indexed-mesh-gles2-apple COMMAND cc-indexed-mesh-gles2-apple-test)
    set_tests_properties(cc-indexed-mesh-gles2-apple PROPERTIES SKIP_RETURN_CODE 77)
endif()

# Texture metadata is independent of a graphics context and is tested on every host.
add_executable(cc-texture-dimensions-test platform/texture_dimensions_test.c
    "${common_root}/src/platform/gles2/texture_dimensions.c")
target_include_directories(cc-texture-dimensions-test PRIVATE
    "${common_root}/src/platform/gles2")
target_compile_features(cc-texture-dimensions-test PRIVATE c_std_11)
if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(cc-texture-dimensions-test PRIVATE
        -Wall -Wextra -Wpedantic -Wconversion -UNDEBUG)
endif()
add_test(NAME cc-texture-dimensions COMMAND cc-texture-dimensions-test)

foreach(module writer writer_io queue jpeg recording recording_half audio_buffer)
    if(WIN32 AND module MATCHES "^(writer_io|queue|recording|recording_half|audio_buffer)$")
        continue()
    endif()
    string(REPLACE "_" "-" test_label "${module}")
    set(test_target "cc-capture-${test_label}-test")
    set(output_directory "${CMAKE_BINARY_DIR}/tests/capture-${test_label}")
    file(MAKE_DIRECTORY "${output_directory}")
    add_executable(${test_target} "capture/capture_${module}_test.c")
    target_link_libraries(${test_target} PRIVATE console_common_capture)
    target_include_directories(${test_target} PRIVATE "${common_root}/src")
    if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
        target_compile_options(${test_target} PRIVATE
            -Wall -Wextra -Wpedantic -Wconversion -UNDEBUG)
    endif()
    add_test(NAME "cc-capture-${test_label}"
        COMMAND ${test_target} "${output_directory}")
endforeach()

if(APPLE AND CC_USE_SYSTEM_VIDEO_ENCODER)
    add_executable(cc-capture-video-apple-test capture/capture_video_apple_test.c)
    target_link_libraries(cc-capture-video-apple-test PRIVATE console_common_capture)
    target_include_directories(cc-capture-video-apple-test PRIVATE "${common_root}/src")
    if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
        target_compile_options(cc-capture-video-apple-test PRIVATE
            -Wall -Wextra -Wpedantic -Wconversion -UNDEBUG)
    endif()
    add_test(NAME cc-capture-video-apple COMMAND cc-capture-video-apple-test)
    set_tests_properties(cc-capture-video-apple PROPERTIES SKIP_RETURN_CODE 77)
endif()

# Production ES2 sampling operations run on Apple's desktop GL driver. The test
# adapts precision declarations, then compares pixels with hardware samplers.
if(APPLE)
    add_executable(cc-gles2-sampling-apple-test
        platform/gles2_sampling_apple_test.c)
    target_include_directories(cc-gles2-sampling-apple-test PRIVATE
        "${common_root}/tests/platform/apple_gl_compat"
        "${common_root}/src/platform/gles2"
        "${common_root}/src/render"
        "${common_root}/include")
    target_link_libraries(cc-gles2-sampling-apple-test PRIVATE "-framework OpenGL")
    target_compile_features(cc-gles2-sampling-apple-test PRIVATE c_std_11)
    if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
        target_compile_options(cc-gles2-sampling-apple-test PRIVATE
            -Wall -Wextra -Wpedantic -Wconversion -Wno-deprecated-declarations -UNDEBUG)
    endif()
    add_test(NAME cc-gles2-sampling-apple COMMAND cc-gles2-sampling-apple-test)
    set_tests_properties(cc-gles2-sampling-apple PROPERTIES SKIP_RETURN_CODE 77)
endif()

if(APPLE)
    add_executable(cc-gles2-tev-precision-apple-test
        platform/gles2_tev_precision_apple_test.m)
    target_include_directories(cc-gles2-tev-precision-apple-test PRIVATE
        "${common_root}/tests/platform/apple_gl_compat"
        "${common_root}/src/platform/gles2"
        "${common_root}/src/render"
        "${common_root}/include")
    target_link_libraries(cc-gles2-tev-precision-apple-test PRIVATE
        "-framework OpenGL" "-framework Metal" "-framework Foundation")
    if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
        target_compile_options(cc-gles2-tev-precision-apple-test PRIVATE
            -Wall -Wextra -Wpedantic -Wconversion -Wno-deprecated-declarations
            -UNDEBUG -fobjc-arc)
    endif()
    add_test(NAME cc-gles2-tev-precision-apple
        COMMAND cc-gles2-tev-precision-apple-test)
    set_tests_properties(cc-gles2-tev-precision-apple PROPERTIES SKIP_RETURN_CODE 77)
endif()
