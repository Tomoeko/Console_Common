cmake_minimum_required(VERSION 3.20)

project(ConsoleCommon VERSION 0.1.0 LANGUAGES C)

# Default projection remains compatible; consumers choose their dimensions.
set(CC_FRAME_WIDTH 640 CACHE STRING "Logical framebuffer width")
set(CC_FRAME_HEIGHT 456 CACHE STRING "Logical framebuffer height")
set(CC_ASPECT_WIDTH 16 CACHE STRING "Displayed aspect numerator")
set(CC_ASPECT_HEIGHT 9 CACHE STRING "Displayed aspect denominator")
foreach(value CC_FRAME_WIDTH CC_FRAME_HEIGHT CC_ASPECT_WIDTH CC_ASPECT_HEIGHT)
    if(NOT "${${value}}" MATCHES "^[1-9][0-9]*$")
        message(FATAL_ERROR "${value} must be a positive integer.")
    endif()
endforeach()

add_library(console_common STATIC
    src/capture/audio_buffer.c
    src/platform/framebuffer.c
    src/render/clip.c
    src/render/frame_damage.c
    src/render/image.c
    src/render/material.c
    src/render/material_blend.c
    src/render/texture_cache.c
    src/render/texture_source.c
    src/render/viewport.c
    src/resources/gx_texture.c
    src/resources/resource_tpl.c
    src/support/atomic_file.c
    src/support/error.c
    src/support/json.c
    src/support/regular_file.c
    src/support/sha1.c
    src/support/utf8.c
)
target_compile_features(console_common PUBLIC c_std_11)
set_target_properties(console_common PROPERTIES C_EXTENSIONS OFF)
target_include_directories(console_common PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_compile_definitions(console_common PUBLIC
    CC_FRAME_WIDTH=${CC_FRAME_WIDTH}
    CC_FRAME_HEIGHT=${CC_FRAME_HEIGHT}
    CC_ASPECT_WIDTH=${CC_ASPECT_WIDTH}
    CC_ASPECT_HEIGHT=${CC_ASPECT_HEIGHT})
if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(console_common PRIVATE
        -Wall -Wextra -Wpedantic -Wconversion)
endif()
if(UNIX AND NOT APPLE)
    target_link_libraries(console_common PUBLIC m)
endif()

option(CC_USE_SYSTEM_VIDEO_ENCODER "Use the Apple H.264 encoder for MP4 recording"
    ${APPLE})
find_package(Threads REQUIRED)
add_library(console_common_capture STATIC
    src/capture/capture_jpeg.c
    src/capture/capture_queue.c
    src/capture/capture_writer.c
    src/capture/recording.c)
target_compile_features(console_common_capture PUBLIC c_std_11)
set_target_properties(console_common_capture PROPERTIES C_EXTENSIONS OFF)
target_include_directories(console_common_capture PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(console_common_capture PUBLIC console_common
    PRIVATE Threads::Threads)
if(APPLE AND CC_USE_SYSTEM_VIDEO_ENCODER)
    target_sources(console_common_capture PRIVATE
        src/platform/apple/capture_video_apple.c)
    target_link_libraries(console_common_capture PRIVATE
        "-framework VideoToolbox"
        "-framework CoreMedia"
        "-framework CoreVideo"
        "-framework CoreFoundation")
else()
    target_sources(console_common_capture PRIVATE src/capture/capture_video_portable.c)
endif()
if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(console_common_capture PRIVATE
        -Wall -Wextra -Wpedantic -Wconversion)
endif()

# A backend belongs to the consumer executable, allowing headless test stubs.
macro(cc_add_platform target backend)
    get_target_property(common_root console_common SOURCE_DIR)
    target_link_libraries(${target} PRIVATE console_common)
    target_include_directories(${target} PRIVATE "${common_root}/src/render")
    if("${backend}" STREQUAL "metal")
        if(NOT APPLE)
            message(FATAL_ERROR "Metal requires an Apple platform.")
        endif()
        enable_language(OBJC)
        target_sources(${target} PRIVATE
            "${common_root}/src/platform/metal/platform_metal.m"
            "${common_root}/src/platform/metal/pipeline.m"
            "${common_root}/src/platform/metal/platform_metal_window.m"
            "${common_root}/src/platform/metal/shaders.m")
        set_source_files_properties(
            "${common_root}/src/platform/metal/platform_metal.m"
            "${common_root}/src/platform/metal/pipeline.m"
            "${common_root}/src/platform/metal/platform_metal_window.m"
            "${common_root}/src/platform/metal/shaders.m"
            PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
        target_link_libraries(${target} PRIVATE
            "-framework AppKit"
            "-framework Metal"
            "-framework QuartzCore"
            "-framework Foundation")
    elseif("${backend}" STREQUAL "gles2")
        if(APPLE)
            message(FATAL_ERROR "The macOS SDK has no OpenGL ES 2.0 runtime; use Metal.")
        endif()
        find_package(X11 REQUIRED)
        find_library(CC_EGL_LIBRARY EGL REQUIRED)
        find_library(CC_GLES2_LIBRARY GLESv2 REQUIRED)
        target_sources(${target} PRIVATE
            "${common_root}/src/platform/gles2/platform_gles2.c"
            "${common_root}/src/platform/gles2/retained_frame.c"
            "${common_root}/src/platform/gles2/host.c"
            "${common_root}/src/platform/gles2/shaders.c"
            "${common_root}/src/platform/gles2/texture_dimensions.c")
        target_link_libraries(${target} PRIVATE
            X11::X11 ${CC_EGL_LIBRARY} ${CC_GLES2_LIBRARY} m)
    else()
        message(FATAL_ERROR "Graphics backend must be metal or gles2.")
    endif()
endmacro()

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    include(CTest)
    if(BUILD_TESTING)
        add_subdirectory(tests)
    endif()
endif()
