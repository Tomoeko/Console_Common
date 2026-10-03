get_target_property(common_root console_common SOURCE_DIR)

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
    platform/framebuffer
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
        "${common_root}/src/render"
        "${common_root}/src/platform")
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
        "${common_root}/src/platform/metal")
    set_source_files_properties(platform/metal_pipeline_test.m
        PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
    target_compile_options(cc-metal-pipeline-test PRIVATE -UNDEBUG)
    add_test(NAME cc-metal-pipeline COMMAND cc-metal-pipeline-test)
    set_tests_properties(cc-metal-pipeline PROPERTIES SKIP_RETURN_CODE 77)
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
