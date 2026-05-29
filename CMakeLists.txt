cmake_minimum_required(VERSION 3.10)
project(alldemo C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)

add_executable(alldemo
    src/alldemo.c
    src/legacy/alldemo_legacy.c
    src/app/loop_player.c
    src/app/page_registry.c
    src/app/page_runtime.c
    src/app/tile_state.c
    src/pages/page_ops.c
    src/pages/page_legacy_proxy.c
    src/pages/page_overlay.c
    src/pages/page_surface.c
    src/pages/page_avm2d.c
    src/pages/page_blend_pyr.c
    src/pages/page_cap_dehaze_offline.c
    src/pages/page_clahe.c
    src/pages/page_conv_cl.c
    src/pages/page_csc.c
    src/pages/page_dualview.c
    src/pages/page_edof_cl.c
    src/pages/page_eis.c
    src/pages/page_eis_detect_npu.c
    src/pages/page_eis_vi.c
    src/pages/page_highlight_suppress.c
    src/pages/page_highlight_suppress_vi.c
    src/pages/page_info.c
    src/pages/page_license.c
    src/pages/page_mcf_fusion_cl.c
    src/pages/page_npu.c
    src/pages/page_osd.c
    src/pages/page_pano.c
    src/pages/page_segment_npu.c
    src/pages/page_rga.c
    src/pages/page_resize_rga.c
    src/pages/page_retinex.c
    src/pages/page_retinex_offline.c
    src/pages/page_stereo_3d.c
    src/pages/page_thermal.c
    src/pages/page_thermal_lowlight_fusion_cl.c
    src/pages/page_thermal_sr_npu.c
    src/pages/page_tnr_cl.c
    src/pages/page_transform.c
    src/pages/page_vi.c
    src/pages/page_vmix.c
    src/pages/page_vo.c
    src/pages/page_vpss.c
    src/pages/page_wbc.c
)
add_executable(wbc_h264_record tools/wbc_h264_record.c)

set(ALDDEMO_INCLUDE_DIRS
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    /usr/include/libdrm
    /usr/include/freetype2
    /userdata/rknpu2/runtime/RK3588/Linux/librknn_api/include
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
