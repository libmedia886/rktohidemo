#include "page_registry.h"

#include <stddef.h>
#include <stdlib.h>
#include <strings.h>

#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))

#define PAGE_ROTATE_SECONDS 8
#define CUSTOMER_ROTATE_SECONDS 10
#define CONV_CL_KERNEL_STAGE_SECONDS 10
#define CONV_CL_KERNEL_STAGE_COUNT 3
#define RGA_OP_SECONDS 3
#define RGA_DEMO_OP_COUNT 7
#define RETINEX_OFFLINE_SECONDS 1
#define RETINEX_OFFLINE_TARGET_SAMPLES 100

static const char *g_module_pages[] = {
    "VI", "VPSS", "VO", "WBC", "RGA", "RESIZE_RGA", "CSC_RGA", "CSC_CL", "OSD",
    "CLAHE", "RETINEX", "RETINEX_OFFLINE", "TNR_CL", "HIGHLIGHT_SUPPRESS",
    "HIGHLIGHT_SUPPRESS_VI", "EIS", "EIS_VI", "CAP_DEHAZE", "CAP_DEHAZE_OFFLINE",
    "DCP_FAST_DEHAZE", "THERMAL", "THERMAL_SR_NPU", "DETECT_NPU", "CONV_CL", "TRANSFORM", "BLEND_PYR", "EDOF_CL",
    "EXPOSURE_FUSION_CL", "MCF_FUSION_CL", "DUALVIEW", "STEREO_3D", "VMIX",
    "VMIX_RGA", "PANO", "AVM", "AVM2D", "SVM3D", "VENC", "VDEC",
    "RTSP_SEND", "RTSP_RECV", "PIC_IO", "LICENSE",
};

static const char *g_default_pages[] = {
    "VI", "VPSS", "VMIX", "OSD", "RGA", "RESIZE_RGA", "CSC_CL", "CLAHE",
    "RETINEX", "RETINEX_OFFLINE", "TNR_CL", "HIGHLIGHT_SUPPRESS_VI",
    "CAP_DEHAZE", "CAP_DEHAZE_OFFLINE", "CONV_CL", "TRANSFORM", "THERMAL", "THERMAL_SR_NPU", "DETECT_NPU",
    "EDOF_CL", "MCF_FUSION_CL", "PANO", "AVM2D",
};

static const char *g_engineering_pages[] = {
    "VI", "VPSS", "VO", "WBC", "OSD", "RESIZE_RGA", "THERMAL", "BLEND_PYR",
    "EDOF_CL", "MCF_FUSION_CL", "RGA", "CSC_RGA", "CSC_CL", "CLAHE",
    "RETINEX", "RETINEX_OFFLINE", "TNR_CL", "HIGHLIGHT_SUPPRESS",
    "HIGHLIGHT_SUPPRESS_VI", "EIS", "EIS_VI", "CAP_DEHAZE",
    "CAP_DEHAZE_OFFLINE", "CONV_CL", "TRANSFORM", "VMIX", "STEREO_3D",
    "PANO", "AVM2D",
};

static const page_desc_t g_page_descs[] = {
    {"VI",
     "数据流：CAM 640x640 -> VI -> VMIX -> OSD -> VO。",
     "展示重点：确认摄像头输入稳定，显示真实采集链路。",
     PAGE_FLAG_CAMERA | PAGE_FLAG_CAMERA_4K | PAGE_FLAG_VMIX_OSD_DISPLAY,
     PAGE_BIND_VI},
    {"VPSS",
     "数据流：VI 640x640 NV12 -> VPSS四路裁剪/缩放 -> 同屏展示。",
     "展示重点：同一输入同时生成缩放、裁剪、翻转和放大输出。",
     PAGE_FLAG_CAMERA | PAGE_FLAG_CAMERA_4K | PAGE_FLAG_VMIX_OSD_DISPLAY,
     PAGE_BIND_VPSS},
    {"VO",
     "数据流：合成NV12画面送入VO，输出到MIPI/DSI屏。",
     "展示重点：验证显示输出控制和上屏状态。",
     0,
     PAGE_BIND_NONE},
    {"WBC",
     "数据流：VI参考窗 + VO_WBC回抓 -> RGA缩放 -> VMIX/OSD -> VO。",
     "展示重点：从当前显示回抓画面并和实时输入参考窗对照。",
     PAGE_FLAG_CAMERA | PAGE_FLAG_VMIX_OSD_DISPLAY,
     PAGE_BIND_WBC},
    {"OSD",
     "数据流：VI 3840x2160 -> RESIZE_RGA 1080x608 -> OSD动态图层 -> VMIX -> VO。",
     "展示重点：动态叠加图层、状态文字和扫描提示。",
     PAGE_FLAG_CAMERA | PAGE_FLAG_CAMERA_4K | PAGE_FLAG_VMIX_OSD_DISPLAY,
     PAGE_BIND_OSD},
    {"RGA",
     "数据流：VI 640x640 NV12 -> RGA裁剪/缩放/旋转 -> VO。",
     "展示重点：硬件完成裁剪、缩放、旋转和快速搬运。",
     PAGE_FLAG_CAMERA | PAGE_FLAG_CAMERA_4K | PAGE_FLAG_VMIX_OSD_DISPLAY,
     PAGE_BIND_RGA},
    {"RESIZE_RGA",
     "数据流：VI 640x640 NV12 -> RESIZE_RGA裁剪放大 -> VO。",
     "展示重点：移动裁剪窗口并实时硬件放大。",
     PAGE_FLAG_CAMERA | PAGE_FLAG_CAMERA_4K | PAGE_FLAG_VMIX_OSD_DISPLAY,
     PAGE_BIND_RESIZE_RGA},
    {"CSC_RGA",
     "数据流：NV12和ARGB互转，展示RGA颜色转换链路。",
     "展示重点：RGA颜色格式转换链路，NV12与ARGB往返。",
     PAGE_FLAG_CAMERA | PAGE_FLAG_VMIX_OSD_DISPLAY,
     PAGE_BIND_CSC_RGA},
    {"CSC_CL",
     "数据流：VI 3840x2160 -> CSC_CL 4K ARGB -> RESIZE_RGA 1080x1920 -> OSD -> VO。",
     "展示重点：GPU/OpenCL颜色矩阵转换和耗时指标。",
     PAGE_FLAG_CAMERA | PAGE_FLAG_CAMERA_4K | PAGE_FLAG_VMIX_OSD_DISPLAY,
     PAGE_BIND_CSC_CL},
    {"CLAHE",
     "数据流：VI 3840x2160 -> CLAHE增强 -> RESIZE_RGA缩放 -> 上下对比显示。",
     "展示重点：上下对比局部对比度增强效果。",
     PAGE_FLAG_CAMERA | PAGE_FLAG_CAMERA_4K | PAGE_FLAG_VMIX_OSD_DISPLAY,
     PAGE_BIND_CLAHE},
    {"RETINEX",
     "数据流：VI 3840x2160 -> RETINEX校正/直通 -> RESIZE_RGA缩放 -> 单路显示。",
     "展示重点：1秒直通、1秒增强，观察光照校正和暗部细节增强。",
     PAGE_FLAG_CAMERA | PAGE_FLAG_CAMERA_4K | PAGE_FLAG_VMIX_OSD_DISPLAY,
     PAGE_BIND_RETINEX},
    {"RETINEX_OFFLINE",
     "数据流：EXDark静态低照度图 -> RETINEX gain=40 -> 原图/增强图对比。",
     "展示重点：100张EXDark低照度图逐张对比，突出暗部提亮效果。",
     0,
     PAGE_BIND_NONE},
    {"TNR_CL",
     "数据流：合成NV12噪声序列 -> TNR_CL OpenCL时域融合 -> 输入/输出对比。",
     "展示重点：块级运动门控时域降噪，观察静态区域噪声收敛和GPU耗时。",
     0,
     PAGE_BIND_NONE},
    {"HIGHLIGHT_SUPPRESS",
     "数据流：合成NV12强反光场景 -> soft-knee高光压制 -> 输入/输出对比。",
     "展示重点：白色刺眼高光变柔和，同时背景和有色目标不过度变暗。",
     0,
     PAGE_BIND_NONE},
    {"HIGHLIGHT_SUPPRESS_VI",
     "数据流：VI 3840x2160 -> HIGHLIGHT_SUPPRESS bypass/压制 -> RESIZE_RGA -> VO。",
     "展示重点：真实VI画面每1秒在bypass原图和高光压制输出之间切换。",
     PAGE_FLAG_CAMERA | PAGE_FLAG_CAMERA_4K | PAGE_FLAG_VMIX_OSD_DISPLAY,
     PAGE_BIND_HIGHLIGHT_SUPPRESS_VI},
    {"EIS",
     "数据流：H264文件 -> VDEC -> VPSS双路 -> RAW/EIS -> VMIX -> VO。",
     "展示重点：离线抖动视频上下对比，观察EIS稳像输出和处理耗时。",
     0,
     PAGE_BIND_NONE},
    {"EIS_VI",
     "数据流：VI 3840x2160 -> EIS -> VPSS缩放 -> OSD -> VO。",
     "展示重点：真实相机输入的电子稳像链路和帧计数。",
     PAGE_FLAG_CAMERA | PAGE_FLAG_CAMERA_4K | PAGE_FLAG_VMIX_OSD_DISPLAY,
     PAGE_BIND_NONE},
    {"CAP_DEHAZE",
     "数据流：VI 1920x1080有效画面 -> CSC_RGA BGR888 -> CAP_DEHAZE -> VO。",
     "展示重点：实时去雾前后对比，突出低能见度细节。",
     PAGE_FLAG_CAMERA | PAGE_FLAG_CAMERA_1080P | PAGE_FLAG_VMIX_OSD_DISPLAY,
     PAGE_BIND_CAP_DEHAZE},
    {"CAP_DEHAZE_OFFLINE",
     "数据流：静态低能见度图 -> CAP_DEHAZE refine_scale=0.25 -> 原图/输出图对比。",
     "展示重点：用固定低能见度图片看清CAP去雾前后差异。",
     0,
     PAGE_BIND_NONE},
    {"DCP_FAST_DEHAZE",
     "数据流：VI 3840x2160 -> CSC_RGA BGR888 -> DCP_FAST_DEHAZE -> RESIZE_RGA -> VO。",
     "展示重点：暗通道快速去雾前后对比。",
     PAGE_FLAG_CAMERA | PAGE_FLAG_VMIX_OSD_DISPLAY,
     PAGE_BIND_DCP_DEHAZE},
    {"THERMAL",
     "数据流：同一红外灰度图映射到多种热成像伪彩模式。",
     "展示重点：同一灰度输入映射为多种热成像色表。",
     0,
     PAGE_BIND_NONE},
    {"THERMAL_SR_NPU",
     "数据流：320x256热成像灰度图 -> THERMAL_SR_NPU -> 1280x1024超分输出。",
     "展示重点：RK3588 NPU四倍超分，原始低分辨率灰度图和NPU灰度输出上下对比。",
     0,
     PAGE_BIND_NONE},
    {"CONV_CL",
     "数据流：VI 640x640 -> RGBA -> CONV_CL四核卷积 -> 2x2输出。",
     "展示重点：四路GPU卷积同屏比较。",
     PAGE_FLAG_CAMERA | PAGE_FLAG_CAMERA_4K,
     PAGE_BIND_NONE},
    {"TRANSFORM",
     "数据流：VI 3840x2160 -> VPSS -> TRANSFORM LUT -> RESIZE_RGA -> VO。",
     "展示重点：4K LUT变换每秒切换原图、畸变矫正、旋转缩放和透视模式。",
     PAGE_FLAG_CAMERA | PAGE_FLAG_CAMERA_4K | PAGE_FLAG_VMIX_OSD_DISPLAY,
     PAGE_BIND_TRANSFORM},
    {"VMIX",
     "数据流：VI 640x640 -> VPSS四路 -> VMIX固定2x2合成 -> VO。",
     "展示重点：四路实时输入由VMIX合成，观察位置、层级和alpha效果。",
     PAGE_FLAG_CAMERA | PAGE_FLAG_CAMERA_4K | PAGE_FLAG_VMIX_OSD_DISPLAY,
     PAGE_BIND_VMIX},
    {"VMIX_RGA",
     "数据流：多路输入经VMIX_RGA合成后送OSD/VO显示。",
     "展示重点：验证RGA路径的多输入合成和布局能力。",
     0,
     PAGE_BIND_NONE},
    {"BLEND_PYR",
     "数据流：同一EDOF基准图生成左清晰/右清晰输入 + 软边mask -> BLEND_PYR。",
     "展示重点：对比线性alpha和金字塔融合在清晰度接缝处的差异。",
     0,
     PAGE_BIND_NONE},
    {"EDOF_CL",
     "数据流：近焦图和远焦图进入EDOF_CL，输出融合清晰图。",
     "展示重点：近焦和远焦图融合成更清晰输出。",
     0,
     PAGE_BIND_NONE},
    {"MCF_FUSION_CL",
     "数据流：彩色图和单色细节图进入MCF_FUSION_CL，输出彩色细节增强图。",
     "展示重点：单色高频细节注入彩色图，观察纹理增强和GPU耗时。",
     0,
     PAGE_BIND_NONE},
    {"EXPOSURE_FUSION_CL",
     "数据流：多曝光输入进入EXPOSURE_FUSION_CL，输出亮暗细节融合图。",
     "展示重点：验证多曝光融合能力和高低亮度区域细节保留。",
     0,
     PAGE_BIND_NONE},
    {"DUALVIEW",
     "数据流：两路RGB输入生成左右并排和逐行交错输出。",
     "展示重点：两路输入生成左右并排和逐行交错输出。",
     0,
     PAGE_BIND_NONE},
    {"STEREO_3D",
     "数据流：VI经VPSS分两路，原图和旋转图进入STEREO_3D合并输出。",
     "展示重点：原图和旋转分支合并为一个3D格式输出。",
     PAGE_FLAG_CAMERA | PAGE_FLAG_VMIX_OSD_DISPLAY,
     PAGE_BIND_STEREO_3D},
    {"PANO",
     "数据流：六路图像按PTO标定查表拼接成全景输出。",
     "展示重点：六张标定图拼接为完整全景。",
     0,
     PAGE_BIND_NONE},
    {"AVM",
     "数据流：四路车身周围输入进入AVM，输出2D环视合成结果。",
     "展示重点：验证泊车环视输入组织和2D鸟瞰拼接能力。",
     0,
     PAGE_BIND_NONE},
    {"AVM2D",
     "数据流：四路泊车视频帧 -> AVM2D研究样例 -> BEV俯视输出。",
     "展示重点：循环播放四路输入和俯视结果，观察车身周围区域拼接。",
     0,
     PAGE_BIND_NONE},
    {"SVM3D",
     "数据流：多视角环视输入和标定资产进入SVM3D，输出3D环视结果。",
     "展示重点：验证3D环视资产和模块可创建状态。",
     0,
     PAGE_BIND_NONE},
    {"DETECT_NPU",
     "数据流：H264解码，经RGA预处理后送DETECT_NPU检测并叠框，底层复用MEDIA_NPU/RKNN运行时。",
     "展示重点：检测框、类别、置信度和NPU帧计数叠加显示。",
     0,
     PAGE_BIND_NONE},
    {"VENC",
     "数据流：合成NV12帧进入VENC，输出H.264码流。",
     "展示重点：验证编码通道创建和H.264输出能力。",
     0,
     PAGE_BIND_NONE},
    {"VDEC",
     "数据流：H.264码流进入VDEC，解码为NV12帧供后级处理。",
     "展示重点：验证解码通道创建和视频输入链路。",
     0,
     PAGE_BIND_NONE},
    {"RTSP_SEND",
     "数据流：编码码流通过RTSP_SEND发布为网络流。",
     "展示重点：验证RTSP发送端口、URL和客户端输出能力。",
     0,
     PAGE_BIND_NONE},
    {"RTSP_RECV",
     "数据流：RTSP_RECV拉取网络流，输出码流或帧给后级模块。",
     "展示重点：验证RTSP接收入口和网络回环能力。",
     0,
     PAGE_BIND_NONE},
    {"PIC_IO",
     "数据流：图片文件读写和序列帧资产进入离线页面或录制链路。",
     "展示重点：验证图片输入输出能力和素材读写路径。",
     0,
     PAGE_BIND_NONE},
    {"LICENSE",
     "数据流：读取本机授权文件，确定可用模块能力。",
     "展示重点：验证license存在、模块授权位和基础运行环境。",
     0,
     PAGE_BIND_NONE},
};

static const demo_loop_t g_customer_loop = {
    "customer demo",
    "CUSTOMER",
    g_default_pages,
    (int)ARRAY_SIZE(g_default_pages),
    CUSTOMER_ROTATE_SECONDS,
};

static const demo_loop_t g_engineering_loop = {
    "engineering demo",
    "ENGINEERING",
    g_engineering_pages,
    (int)ARRAY_SIZE(g_engineering_pages),
    PAGE_ROTATE_SECONDS,
};

const demo_loop_t *alldemo_customer_loop(void) {
    return &g_customer_loop;
}

const demo_loop_t *alldemo_engineering_loop(void) {
    return &g_engineering_loop;
}

int loop_page_rotate_seconds(const demo_loop_t *loop, const char *module) {
    int seconds = loop ? loop->rotate_seconds : CUSTOMER_ROTATE_SECONDS;
    if (module && strcasecmp(module, "RGA") == 0) {
        int full_cycle = RGA_OP_SECONDS * RGA_DEMO_OP_COUNT;
        if (seconds < full_cycle) seconds = full_cycle;
    }
    if (module && strcasecmp(module, "CONV_CL") == 0) {
        int full_cycle = CONV_CL_KERNEL_STAGE_SECONDS * CONV_CL_KERNEL_STAGE_COUNT;
        if (seconds < full_cycle) seconds = full_cycle;
    }
    if (module && strcasecmp(module, "RETINEX_OFFLINE") == 0) {
        int full_cycle = RETINEX_OFFLINE_SECONDS * RETINEX_OFFLINE_TARGET_SAMPLES;
        if (seconds < full_cycle) seconds = full_cycle;
    }
    return seconds;
}

int module_page_number(const char *name) {
    const char *env = getenv(NAV_ENV_PAGE);
    if (env && env[0]) {
        char *end = NULL;
        long v = strtol(env, &end, 10);
        if (end != env && v > 0 && v < 1000) return (int)v;
    }
    if (!name) return 1;
    name = canonical_tile_name(name);
    for (size_t i = 0; i < ARRAY_SIZE(g_module_pages); ++i) {
        if (strcasecmp(g_module_pages[i], name) == 0) return (int)i + 1;
    }
    return 1;
}

int module_page_total(void) {
    const char *env = getenv(NAV_ENV_TOTAL);
    if (env && env[0]) {
        char *end = NULL;
        long v = strtol(env, &end, 10);
        if (end != env && v > 0 && v < 1000) return (int)v;
    }
    return (int)ARRAY_SIZE(g_module_pages);
}

const char *showcase_nav_mode(void) {
    const char *mode = getenv(NAV_ENV_MODE);
    const char *profile = getenv(NAV_ENV_PROFILE);
    if (!mode || !mode[0]) return NULL;
    if (profile && strcasecmp(profile, "CUSTOMER") == 0) {
        return strcasecmp(mode, "MANUAL") == 0 ? "CUSTOMER MANUAL" : "CUSTOMER DEMO";
    }
    if (profile && strcasecmp(profile, "ENGINEERING") == 0) {
        return strcasecmp(mode, "MANUAL") == 0 ? "ENGINEER MANUAL" : "ENGINEER DEMO";
    }
    if (strcasecmp(mode, "MANUAL") == 0) return "MANUAL";
    if (strcasecmp(mode, "AUTO") == 0) return "AUTO LOOP";
    return NULL;
}

int default_page_index(const char *name) {
    if (!name) return -1;
    name = canonical_tile_name(name);
    for (size_t i = 0; i < ARRAY_SIZE(g_default_pages); ++i) {
        if (strcasecmp(g_default_pages[i], name) == 0) return (int)i;
    }
    return -1;
}

int alldemo_default_page_count(void) {
    return (int)ARRAY_SIZE(g_default_pages);
}

const char *alldemo_default_page_name(int index) {
    if (index < 0 || index >= (int)ARRAY_SIZE(g_default_pages)) return NULL;
    return g_default_pages[index];
}

const page_desc_t *page_desc_find(const char *name) {
    if (!name || !*name) return NULL;
    if (strcasecmp(name, "NPU") == 0 || strcasecmp(name, "YOLO_DETECT_NPU") == 0) {
        name = "DETECT_NPU";
    }
    for (size_t i = 0; i < ARRAY_SIZE(g_page_descs); ++i) {
        if (strcasecmp(g_page_descs[i].name, name) == 0) return &g_page_descs[i];
    }
    return NULL;
}

int page_has_flag(const char *name, unsigned flag) {
    const page_desc_t *desc = page_desc_find(name);
    return desc && ((desc->flags & flag) != 0);
}

page_bind_display_t page_bind_display(const char *name) {
    const page_desc_t *desc = page_desc_find(name);
    return desc ? desc->bind_display : PAGE_BIND_NONE;
}

const char *module_flow_note(const char *name) {
    const page_desc_t *desc = page_desc_find(name);
    return desc && desc->flow_note ?
        desc->flow_note : "数据流：模块输入 -> 算法处理 -> 显示输出。";
}

const char *module_showcase_note(const char *name) {
    const page_desc_t *desc = page_desc_find(name);
    return desc && desc->showcase_note ?
        desc->showcase_note : "展示重点：观察输入、处理结果和实时性能。";
}

static int is_mcf_tile_name(const char *name) {
    return name && (strcasecmp(name, "MCF") == 0 ||
                    strcasecmp(name, "MCF_FUSION_CL") == 0);
}

const char *canonical_tile_name(const char *name) {
    if (is_mcf_tile_name(name)) return "MCF_FUSION_CL";
    if (name && strcasecmp(name, "VI_HIGHLIGHT_SUPPRESS") == 0) return "HIGHLIGHT_SUPPRESS_VI";
    if (name && (strcasecmp(name, "NPU") == 0 || strcasecmp(name, "YOLO_DETECT_NPU") == 0)) return "DETECT_NPU";
    return name;
}

int tile_needs_camera(const char *name) {
    if (!name) return 1;
    if (strcasecmp(name, "EIS_VI") == 0) return 1;
    return page_has_flag(name, PAGE_FLAG_CAMERA);
}

int tile_uses_4k_camera_input(const char *name) {
    if (!name) return 0;
    if (strcasecmp(name, "EIS_VI") == 0) return 1;
    return page_has_flag(name, PAGE_FLAG_CAMERA_4K);
}

int tile_uses_1080p_camera_input(const char *name) {
    if (!name) return 0;
    return page_has_flag(name, PAGE_FLAG_CAMERA_1080P);
}
