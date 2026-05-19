cmake_minimum_required(VERSION 3.10)
project(alldemo C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)

add_executable(alldemo src/alldemo.c)
add_executable(wbc_h264_record tools/wbc_h264_record.c)

set(ALDDEMO_INCLUDE_DIRS
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    /usr/include/libdrm
    /usr/include/freetype2
)

target_include_directories(alldemo PRIVATE ${ALDDEMO_INCLUDE_DIRS})
target_include_directories(wbc_h264_record PRIVATE ${ALDDEMO_INCLUDE_DIRS})

set(ALDDEMO_MEDIA_LIBS
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/libmedia.a
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/libparrot_rtsp.a
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/libparrot_pomp.a
    drm
    rockchip_mpp
    dbus-1
    json-c
    jpeg
    png
    glib-2.0
    vulkan
    rga
    OpenCL
    crypto
    EGL
    gbm
    freetype
    rknnrt
    rt
    m
    pthread
)

target_link_libraries(alldemo ${ALDDEMO_MEDIA_LIBS})
target_link_libraries(wbc_h264_record ${ALDDEMO_MEDIA_LIBS})

set_target_properties(alldemo PROPERTIES
    BUILD_RPATH "${CMAKE_CURRENT_SOURCE_DIR}/lib"
    INSTALL_RPATH "$ORIGIN/../lib"
)
set_target_properties(wbc_h264_record PROPERTIES
    BUILD_RPATH "${CMAKE_CURRENT_SOURCE_DIR}/lib"
    INSTALL_RPATH "$ORIGIN/../lib"
)
