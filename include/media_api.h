#ifndef MEDIA_API_H
#define MEDIA_API_H

/*
	本库为闭源商业软件。
	非商业用途：可在遵守 LICENSE 前提下使用（如你提供免费）。
	商业用途：必须购买商业授权，否则属于未经许可使用。
	严禁逆向、破解、绕过授权、篡改或未经许可分发；违者将依法追责并主张违约金（详见 LICENSE.txt）。
*/
#include <stdint.h>
#include <stddef.h>
#include <linux/dma-buf.h>

// RTSP 传输模式
typedef enum {
    RTSP_TRANSPORT_UDP = 0,     // 仅 UDP
    RTSP_TRANSPORT_TCP = 1,     // 仅 TCP (interleaved mode)
} rtsp_transport_mode_t;

#define MEDIA_API_VERSION_MAJOR 0
#define MEDIA_API_VERSION_MINOR 1
#define MEDIA_API_VERSION_PATCH 0
#define MEDIA_API_VERSION_STR "0.1.2"

typedef int MEDIA_DEV;
typedef int MEDIA_CHN;

typedef struct {
    int pool_id;
    int index;
} MEDIA_BUFFER;

typedef struct {
    void *data;
    size_t len;
    void *handle;
} MEDIA_PACKET;

typedef enum {
    MEDIA_FORMAT_UNSET = 0,  // 未设置格式
    MEDIA_FORMAT_NV12 = 1,
    MEDIA_FORMAT_RGB888 = 2,
    MEDIA_FORMAT_RGBA8888 = 3,
    MEDIA_FORMAT_AFBC = 4,
    MEDIA_FORMAT_GRAY8 = 5,  // 8 位灰度格式
    MEDIA_FORMAT_BGR888 = 6,
    MEDIA_FORMAT_BGRA8888 = 7,
    MEDIA_FORMAT_YUV422SP = 8,
    MEDIA_FORMAT_YUV422P = 9,
    MEDIA_FORMAT_YUV420SP = 10,
    MEDIA_FORMAT_YUV420P = 11,
    MEDIA_FORMAT_YCrCb_420SP = 12,
    MEDIA_FORMAT_YCrCb_422SP = 13,
    MEDIA_FORMAT_RGB565 = 14,
    MEDIA_FORMAT_RGBA5551 = 15,
    MEDIA_FORMAT_RGBA4444 = 16,
    MEDIA_FORMAT_ARGB8888 = 17,
    MEDIA_FORMAT_ABGR8888 = 18,
    // 编码格式（用于模块间传递编码包）
    MEDIA_FORMAT_H264 = 100,  // H.264 编码包
    MEDIA_FORMAT_H265 = 101,  // H.265 编码包
} MEDIA_FORMAT;

typedef enum {
    MEDIA_VIDEO_H264 = 0,
    MEDIA_VIDEO_H265 = 1,
    MEDIA_VIDEO_MJPEG = 2,
    MEDIA_VIDEO_JPEG = 3,
} MEDIA_VIDEO_TYPE;

typedef enum {
    MEDIA_IMAGE_CODEC_AUTO = 0,
    MEDIA_IMAGE_CODEC_JPEG = 1,
    MEDIA_IMAGE_CODEC_PNG = 2,
    MEDIA_IMAGE_CODEC_BMP = 3,
} MEDIA_IMAGE_CODEC;

typedef enum {
    MEDIA_VENC_RC_CBR = 0,        // 固定码率 - 保持恒定码率，适合网络流媒体
    MEDIA_VENC_RC_VBR = 1,        // 可变码率 - 根据画面复杂度调整，画质优先
    MEDIA_VENC_RC_FIXQP = 2,      // 固定 QP - 固定量化参数，画质稳定但码率波动大
    MEDIA_VENC_RC_AVBR = 3,       // 平均可变码率 - 长期平均码率可控，短期可波动
    MEDIA_VENC_RC_CBR_SHARE = 4,  // 共享 CBR - 多通道共享带宽的 CBR 模式
    MEDIA_VENC_RC_VBR_SHARE = 5,  // 共享 VBR - 多通道共享带宽的 VBR 模式
} MEDIA_VENC_RC_MODE;

#define MEDIA_VPSS_MAX_OUTPUTS 16
#define MEDIA_RGA_MAX_INPUTS 4
#define MEDIA_RGA_MAX_OUTPUTS 4
#define MEDIA_NPU_MAX_TOPK 64
#define MEDIA_NPU_MAX_OBJECTS 256
#define MEDIA_NPU_MAX_TENSORS 16
#define MEDIA_NPU_MAX_DIMS 8

typedef struct {
    const char *device;
    int width;
    int height;
    int stride;     // bytesperline for NV12 (0=driver default)
    int fps;
    int buf_cnt;
    int pool_id;
    int format;     // 像素格式 (V4L2_PIX_FMT_*)
} MEDIA_VI_ATTR;

typedef struct {
    const char *folder_path;  // required
    int image_codec;          // MEDIA_IMAGE_CODEC_* (AUTO/JPEG/PNG/BMP)
    int width;                // required > 0
    int height;               // required > 0
    int stride;               // required > 0
    int format;               // MEDIA_FORMAT_RGB888/BGR888/NV12
    int fps;                  // required > 0
    int pool_id;              // required >= 0
} MEDIA_PIC_SEND_ATTR;

typedef struct {
    const char *folder_path;  // required
    int image_codec;          // MEDIA_IMAGE_CODEC_JPEG/PNG/BMP
    int width;                // required > 0
    int height;               // required > 0
    int stride;               // required > 0
    int format;               // MEDIA_FORMAT_RGB888/BGR888/NV12
    int input_depth;          // required > 0
    int jpeg_quality;         // optional: 1..100 (<=0 -> module default 90)
} MEDIA_PIC_REV_ATTR;

typedef struct {
    int output_id;
    int out_width;
    int out_height;
    int out_stride;
    int pool_id;
    int crop_x;
    int crop_y;
    int crop_w;
    int crop_h;
    int in_fps;   // input fps reference (-1=disable control, 0=not allowed, >0=control fps)
    int out_fps;  // target output fps (-1=disable control, 0=not allowed, >0=control fps)
    int flip_h;
    int flip_v;
    int rotate;
    int output_format;  // 输出格式：MEDIA_FORMAT_NV12 / RGB888/BGR888/RGBA family
} MEDIA_VPSS_OUT_ATTR;

typedef struct {
    int width;
    int height;
    int input_stride;
    int input_depth;   // 输入端口队列深度（0 表示默认=4）
    int input_format;  // 输入格式：MEDIA_FORMAT_NV12
    int in_fps;
    int out_fps;
    int output_count;  // 输出数量（1-MEDIA_VPSS_MAX_OUTPUTS）
    MEDIA_VPSS_OUT_ATTR outputs[MEDIA_VPSS_MAX_OUTPUTS];
} MEDIA_VPSS_ATTR;

typedef struct {
    int width;
    int height;
    int stride;     // bytesperline (0 表示默认=width)
    int fps;
    int buf_cnt;
    int input_depth;  // 输入端口队列深度（0 表示默认=4）
    int bitrate;
    int gop;
    int video_format;  // 编码格式 (MEDIA_FORMAT_H264=100, MEDIA_FORMAT_H265=101)
    int rc_mode;
    int input_format;
} MEDIA_VENC_ATTR;

typedef struct {
    int width;
    int height;
    int stride;     // 输出 stride (0 表示自动计算=64 字节对齐)
    int buf_cnt;
    int video_type; // 视频格式 (0=H264, 1=H265)
    int pool_id;    // 输出 buffer pool ID
    int has_input_port;  // 是否创建输入端口 (1=bind 模式，0=手动模式)
    int input_depth;     // 输入端口队列深度 (0=默认 4)
} MEDIA_VDEC_ATTR;

typedef enum {
    MEDIA_RGA_ALG_RESIZE = 0,
    MEDIA_RGA_ALG_CSC = 1,
    MEDIA_RGA_ALG_ROTATE = 2,
    MEDIA_RGA_ALG_COMPOSE = 3,
    MEDIA_RGA_ALG_COPY = 4,
    MEDIA_RGA_ALG_TRANSLATE = 5,
    MEDIA_RGA_ALG_FLIP = 6,
    MEDIA_RGA_ALG_CROP = 7,
    MEDIA_RGA_ALG_MOSAIC = 8,
    MEDIA_RGA_ALG_PYRAMID = 9,
    MEDIA_RGA_ALG_OSD = 10,
} MEDIA_RGA_ALG;

typedef struct {
    int port_id;
    int width;
    int height;
    int stride;     // bytes per line (0=auto)
    int format;     // MEDIA_FORMAT_*
    int pool_id;    // output pool id; input ignored
    int crop_x;
    int crop_y;
    int crop_w;
    int crop_h;
    int dst_x;
    int dst_y;
    int mosaic_mode; // IM_MOSAIC_*
    int pyramid_dir; // IM_UP_SCALE / IM_DOWN_SCALE
    int rotate;     // 0/90/180/270
    int flip_h;
    int flip_v;
    int queue_depth; // 端口队列深度（0 表示默认=4）
} MEDIA_RGA_PORT_ATTR;

typedef struct {
    int algo;
    int input_count;   // 输入数量（1-MEDIA_RGA_MAX_INPUTS）
    int output_count;  // 输出数量（1-MEDIA_RGA_MAX_OUTPUTS）
    int input_depth;
    int output_depth;
    MEDIA_RGA_PORT_ATTR inputs[MEDIA_RGA_MAX_INPUTS];
    MEDIA_RGA_PORT_ATTR outputs[MEDIA_RGA_MAX_OUTPUTS];
} MEDIA_RGA_GRP_ATTR;

typedef enum {
    MEDIA_NPU_BACKEND_AUTO = 0,
    MEDIA_NPU_BACKEND_RKNN = 1,
    MEDIA_NPU_BACKEND_NNIE = 2,
    MEDIA_NPU_BACKEND_CPU = 3,
} MEDIA_NPU_BACKEND;

typedef enum {
    MEDIA_NPU_TASK_RAW = 0,
    MEDIA_NPU_TASK_CLASSIFY = 1,
    MEDIA_NPU_TASK_DETECT = 2,
    MEDIA_NPU_TASK_SEGMENT = 3,
} MEDIA_NPU_TASK;

typedef enum {
    MEDIA_NPU_LAYOUT_UNSET = 0,
    MEDIA_NPU_LAYOUT_NHWC = 1,
    MEDIA_NPU_LAYOUT_NCHW = 2,
} MEDIA_NPU_LAYOUT;

typedef enum {
    MEDIA_NPU_DATA_TYPE_UNSET = 0,
    MEDIA_NPU_DATA_TYPE_UINT8 = 1,
    MEDIA_NPU_DATA_TYPE_INT8 = 2,
    MEDIA_NPU_DATA_TYPE_FLOAT16 = 3,
    MEDIA_NPU_DATA_TYPE_FLOAT32 = 4,
} MEDIA_NPU_DATA_TYPE;

typedef struct {
    int width;
    int height;
    int format;
    int stride;
    int layout;
    int data_type;
    size_t size;
    int dim_count;
    int dims[MEDIA_NPU_MAX_DIMS];
} MEDIA_NPU_INPUT_ATTR;

typedef struct {
    const char *model_path;
    const char *label_path;
    int backend;
    int task;
    int input_width;
    int input_height;
    int input_format;
    int input_layout;
    int input_depth;
    int passthrough;
    int topk;
    float score_thresh;
    float nms_thresh;
    const char *adapter_name;
    int input_count;
    int dynamic_shape;
    MEDIA_NPU_INPUT_ATTR inputs[MEDIA_NPU_MAX_TENSORS];
} MEDIA_NPU_ATTR;

typedef enum {
    MEDIA_NPU_RESULT_RAW = 0,
    MEDIA_NPU_RESULT_CLASSIFY = 1,
    MEDIA_NPU_RESULT_DETECT = 2,
    MEDIA_NPU_RESULT_SEGMENT = 3,
} MEDIA_NPU_RESULT_TYPE;

typedef struct {
    int class_id;
    float score;
    const char *label;
} MEDIA_NPU_CLASS_ITEM;

typedef struct {
    int class_count;
    int topk_count;
    MEDIA_NPU_CLASS_ITEM topk[MEDIA_NPU_MAX_TOPK];
} MEDIA_NPU_CLASS_RESULT;

typedef struct {
    int class_id;
    float score;
    float x;
    float y;
    float w;
    float h;
    const char *label;
} MEDIA_NPU_OBJECT;

typedef struct {
    int object_count;
    MEDIA_NPU_OBJECT objects[MEDIA_NPU_MAX_OBJECTS];
} MEDIA_NPU_DETECT_RESULT;

typedef struct {
    int width;
    int height;
    int class_count;
    int format;
    MEDIA_BUFFER mask_buf;
} MEDIA_NPU_SEGMENT_RESULT;

typedef struct {
    int index;
    int dtype;
    int layout;
    int dims[8];
    int dim_count;
    void *data;
    size_t size;
    float scale;
    int zero_point;
} MEDIA_NPU_TENSOR;

typedef struct {
    int tensor_count;
    MEDIA_NPU_TENSOR tensors[MEDIA_NPU_MAX_TENSORS];
} MEDIA_NPU_RAW_RESULT;

typedef struct {
    int type;
    uint64_t frame_id;
    uint64_t pts;
    union {
        MEDIA_NPU_RAW_RESULT raw;
        MEDIA_NPU_CLASS_RESULT classify;
        MEDIA_NPU_DETECT_RESULT detect;
        MEDIA_NPU_SEGMENT_RESULT segment;
    } u;
} MEDIA_NPU_RESULT;

typedef struct {
    int backend;
    int task;
    int input_count;
    int output_count;
    int class_count;
    int input_width;
    int input_height;
    int input_format;
    const char *model_name;
    const char *adapter_name;
} MEDIA_NPU_MODEL_INFO;

// 系统控制
int MEDIA_SYS_Init(void);
int MEDIA_SYS_Exit(void);
int MEDIA_SYS_SetLicense(const char *path);
int MEDIA_SYS_DumpChipInfo(const char *path);
const char *MEDIA_SYS_GetVersion(void);
int MEDIA_SYS_GetModuleFrameCount(const char *mod, int id, uint64_t *frame_count);

// 绑定/解绑
int MEDIA_SYS_Bind(const char *src_mod, int src_id, const char *src_port,
                   const char *dst_mod, int dst_id, const char *dst_port);
int MEDIA_SYS_UnBind(const char *src_mod, int src_id, const char *src_port,
                     const char *dst_mod, int dst_id, const char *dst_port);

// VI
int MEDIA_VI_SetAttr(MEDIA_DEV dev, const MEDIA_VI_ATTR *attr);
int MEDIA_VI_Enable(MEDIA_DEV dev);
int MEDIA_VI_Disable(MEDIA_DEV dev);
int MEDIA_VI_GetFrame(MEDIA_DEV dev, MEDIA_BUFFER *buf, int timeout_ms);
int MEDIA_VI_ReleaseFrame(MEDIA_DEV dev, MEDIA_BUFFER buf);

// Pool
int MEDIA_POOL_Create(int pool_id, size_t size, int count);
int MEDIA_POOL_Destroy(int pool_id);
int MEDIA_POOL_Get(int pool_id, MEDIA_BUFFER *buf);
int MEDIA_POOL_Put(MEDIA_BUFFER buf);

// 用户手动申请/归还 Buffer
int MEDIA_POOL_GetBuffer(int pool_id, MEDIA_BUFFER *buf);
int MEDIA_POOL_PutBuffer(MEDIA_BUFFER buf);
int MEDIA_POOL_GetFd(MEDIA_BUFFER buf, int *dmabuf_fd, size_t *size);
size_t MEDIA_POOL_GetSize(MEDIA_BUFFER buf);
void *MEDIA_POOL_GetVaddr(MEDIA_BUFFER buf);
int MEDIA_POOL_BeginCpuAccess(MEDIA_BUFFER buf, uint64_t flags);
int MEDIA_POOL_EndCpuAccess(MEDIA_BUFFER buf, uint64_t flags);
//int MEDIA_POOL_ImportFd(int pool_id, int index, int dmabuf_fd, size_t size);

// VPSS
int MEDIA_VPSS_SetAttr(int grp_id, const MEDIA_VPSS_ATTR *attr);
int MEDIA_VPSS_SetOutCrop(int grp_id, int output_id, int crop_x, int crop_y, int crop_w, int crop_h);
int MEDIA_VPSS_SetOutAttr(int grp_id, const MEDIA_VPSS_OUT_ATTR *attr);
int MEDIA_VPSS_DestroyGrp(int grp_id);
int MEDIA_VPSS_Enable(int grp_id);
int MEDIA_VPSS_Disable(int grp_id);
int MEDIA_VPSS_Group_SendFrame(int grp_id, MEDIA_BUFFER buf, int timeout_ms);
int MEDIA_VPSS_Chn_GetFrame(int grp_id, int output_id, MEDIA_BUFFER *buf, int timeout_ms);
int MEDIA_VPSS_Chn_ReleaseFrame(int grp_id, int output_id, MEDIA_BUFFER buf);

// VENC
int MEDIA_VENC_CreateChn(int chn, int width, int height, int stride, int fps,
                         int buf_cnt, int input_depth, int output_pool_id,
                         int bitrate, int gop, int video_format);
int MEDIA_VENC_SetAttr(int chn, const MEDIA_VENC_ATTR *attr);
int MEDIA_VENC_DestroyChn(int chn);
int MEDIA_VENC_Enable(int chn);
int MEDIA_VENC_Disable(int chn);
int MEDIA_VENC_Start(int chn);
int MEDIA_VENC_Stop(int chn);
int MEDIA_VENC_GetFrame(int chn, MEDIA_BUFFER *buf, int timeout_ms);
int MEDIA_VENC_ReleaseFrame(int chn, MEDIA_BUFFER buf);
int MEDIA_VENC_GetMpp(int chn, void **mpp_ctx, void **mpp_mpi);
int MEDIA_VENC_GetPacket(int chn, MEDIA_PACKET *pkt, int timeout_ms);
int MEDIA_VENC_ReleasePacket(int chn, MEDIA_PACKET *pkt);

// VENC 扩展参数设置接口
int MEDIA_VENC_SetRcMode(int chn, int rc_mode, int bitrate);
int MEDIA_VENC_SetFormat(int chn, int format);
int MEDIA_VENC_SetVideoType(int chn, int video_type);
int MEDIA_VENC_SetQpConfig(int chn, int qp_init, int qp_max, int qp_min);
int MEDIA_VENC_SetBpsConfig(int chn, int bps_max, int bps_min);

// H265 专属参数设置
int MEDIA_VENC_SetH265Profile(int chn, int profile);
int MEDIA_VENC_SetH265Level(int chn, int level);
int MEDIA_VENC_SetH265Tier(int chn, int tier);
int MEDIA_VENC_SetH265CuSize(int chn, int cu_size);
int MEDIA_VENC_SetH265SaoEn(int chn, int sao_en);
int MEDIA_VENC_SetH265DblkEn(int chn, int dblk_en);

// H264 专属参数设置
int MEDIA_VENC_SetH264Profile(int chn, int profile);
int MEDIA_VENC_SetH264Level(int chn, int level);
int MEDIA_VENC_SetH264CabacEn(int chn, int cabac_en);

// VDEC
int MEDIA_VDEC_CreateChn(int chn, const MEDIA_VDEC_ATTR *attr);
int MEDIA_VDEC_SetAttr(int chn, const MEDIA_VDEC_ATTR *attr);
int MEDIA_VDEC_DestroyChn(int chn);
int MEDIA_VDEC_Enable(int chn);
int MEDIA_VDEC_Disable(int chn);
int MEDIA_VDEC_Start(int chn);
int MEDIA_VDEC_Stop(int chn);
int MEDIA_VDEC_SendPacket(int chn, void *data, size_t size, uint64_t pts);

// RTSP_SEND
typedef struct {
    const char *bind_addr;      // 绑定地址，NULL 表示所有接口
    int port;                   // RTSP 端口
    const char *url_suffix;     // URL 后缀，如 "live"
    int width;                  // 视频宽度
    int height;                 // 视频高度
    int fps;                    // 帧率
    int rtp_port_base;          // RTP 起始端口，默认 50000
    int input_depth;            // 输入端口队列深度（0=默认 4, >0=指定深度）
    int max_clients;            // 最大并发客户端数（<=0 按 1 处理）
    int transport_mode;         // 传输模式 (0=UDP, 1=TCP)
    int video_format;           // 视频格式 (MEDIA_FORMAT_H264 / MEDIA_FORMAT_H265)
} MEDIA_RTSP_SEND_ATTR;

int MEDIA_RTSP_SEND_CreateGrp(int grp, const MEDIA_RTSP_SEND_ATTR *attr);
int MEDIA_RTSP_SEND_DestroyGrp(int grp);
int MEDIA_RTSP_SEND_Start(int grp);
int MEDIA_RTSP_SEND_Stop(int grp);
int MEDIA_RTSP_SEND_Enable(int grp);
int MEDIA_RTSP_SEND_Disable(int grp);

// RTSP_RECV
typedef struct {
    const char *url;            // RTSP URL
    int recv_buffer_size;       // 接收缓冲区大小 (0 = 默认 512KB)
    int rtp_port;               // 本地 RTP 端口 (0 = 自动选择)
    int transport_mode;         // 传输模式 (0=UDP, 1=TCP)
    int output_pool_id;         // 输出 buffer pool ID（必须从外部传入）
    int video_format;           // 视频格式 (MEDIA_FORMAT_H264 / MEDIA_FORMAT_H265)
    int width;                  // 视频宽度 (0=从 SDP 解析)
    int height;                 // 视频高度 (0=从 SDP 解析)
    int fps;                    // 帧率 (0=默认 30)
} MEDIA_RTSP_RECV_ATTR;

int MEDIA_RTSP_RECV_CreateGrp(int grp, const MEDIA_RTSP_RECV_ATTR *attr);
int MEDIA_RTSP_RECV_DestroyGrp(int grp);
int MEDIA_RTSP_RECV_Start(int grp);
int MEDIA_RTSP_RECV_Stop(int grp);
int MEDIA_RTSP_RECV_Enable(int grp);
int MEDIA_RTSP_RECV_Disable(int grp);

typedef struct {
    const char *bind_addr;      // 绑定地址，NULL 表示所有接口
    int port;                   // RTSP 端口
    const char *url_suffix;     // URL 后缀，如 "live"
    int width;                  // 视频宽度
    int height;                 // 视频高度
    int fps;                    // 帧率
    int rtp_port_base;          // RTP 起始端口，默认 50000
    int input_depth;            // 输入端口队列深度（0=默认 4, >0=指定深度）
    int max_clients;            // 最大并发客户端数（<=0 按 1 处理）
    int transport_mode;         // 传输模式 (0=UDP, 1=TCP)
    int video_format;           // 视频格式 (MEDIA_FORMAT_H264 / MEDIA_FORMAT_H265)
} MEDIA_RTSP_SEND_LIB_ATTR;

int MEDIA_RTSP_SEND_LIB_CreateGrp(int grp, const MEDIA_RTSP_SEND_LIB_ATTR *attr);
int MEDIA_RTSP_SEND_LIB_DestroyGrp(int grp);
int MEDIA_RTSP_SEND_LIB_Start(int grp);
int MEDIA_RTSP_SEND_LIB_Stop(int grp);
int MEDIA_RTSP_SEND_LIB_Enable(int grp);
int MEDIA_RTSP_SEND_LIB_Disable(int grp);

typedef MEDIA_RTSP_RECV_ATTR MEDIA_RTSP_REV_LIB_ATTR;

int MEDIA_RTSP_REV_LIB_CreateGrp(int grp, const MEDIA_RTSP_REV_LIB_ATTR *attr);
int MEDIA_RTSP_REV_LIB_DestroyGrp(int grp);
int MEDIA_RTSP_REV_LIB_Start(int grp);
int MEDIA_RTSP_REV_LIB_Stop(int grp);
int MEDIA_RTSP_REV_LIB_Enable(int grp);
int MEDIA_RTSP_REV_LIB_Disable(int grp);

// BLEND_PYR
typedef struct {
    int width;
    int height;
    int input_stride;
    int input_depth;
    int input_format;
    int output_stride;
} MEDIA_BLEND_PYR_ATTR;

int MEDIA_BLEND_PYR_SetAttr(int grp_id, const MEDIA_BLEND_PYR_ATTR *attr);
int MEDIA_BLEND_PYR_DestroyGrp(int grp_id);
int MEDIA_BLEND_PYR_Enable(int grp_id);
int MEDIA_BLEND_PYR_Disable(int grp_id);

// BLEND_PYR Mask 设置 (MPI 接口)
int MEDIA_BLEND_PYR_SetMask(int grp_id, MEDIA_BUFFER *mask_buf);
int MEDIA_BLEND_PYR_GetMaskStatus(int grp_id);

// RGA (framework)
int MEDIA_RGA_CreateChn(int chn, int width, int height, int format);
int MEDIA_RGA_SetAttr(int chn, int width, int height, int format);
int MEDIA_RGA_DestroyChn(int chn);
int MEDIA_RGA_Start(int chn);
int MEDIA_RGA_Stop(int chn);
int MEDIA_RGA_CreateGrp(int grp, const MEDIA_RGA_GRP_ATTR *attr);
int MEDIA_RGA_SetGrpAttr(int grp, const MEDIA_RGA_GRP_ATTR *attr);
int MEDIA_RGA_SetInAttr(int grp, int in_idx, const MEDIA_RGA_PORT_ATTR *attr);
int MEDIA_RGA_SetOutAttr(int grp, int out_idx, const MEDIA_RGA_PORT_ATTR *attr);
int MEDIA_RGA_GetFrame(int grp, MEDIA_BUFFER *buf, int timeout_ms);
int MEDIA_RGA_ReleaseFrame(int grp, MEDIA_BUFFER buf);

// NPU generic inference module
int MEDIA_NPU_CreateGrp(int grp, const MEDIA_NPU_ATTR *attr);
int MEDIA_NPU_DestroyGrp(int grp);
int MEDIA_NPU_Start(int grp);
int MEDIA_NPU_Stop(int grp);
int MEDIA_NPU_Enable(int grp);
int MEDIA_NPU_Disable(int grp);
int MEDIA_NPU_SendFrame(int grp, MEDIA_BUFFER buf, int timeout_ms);
int MEDIA_NPU_SendFrameEx(int grp, int input_idx, MEDIA_BUFFER buf, int timeout_ms);
int MEDIA_NPU_GetResult(int grp, MEDIA_NPU_RESULT *result, int timeout_ms);
int MEDIA_NPU_ReleaseResult(int grp, MEDIA_NPU_RESULT *result);
int MEDIA_NPU_GetModelInfo(int grp, MEDIA_NPU_MODEL_INFO *info);
int MEDIA_NPU_GetFrame(int grp, MEDIA_BUFFER *buf, int timeout_ms);
int MEDIA_NPU_ReleaseFrame(int grp, MEDIA_BUFFER buf);

// Retinex (图像增强算法)
typedef struct {
    int scale_count;     // 输入数量：1/2/3
    int width;           // 图像宽度
    int height;          // 图像高度
    int format;          // 图像格式（MEDIA_FORMAT_NV12）
    int output_depth;    // 输出端口队列深度（0 表示默认=4）
    int input_depth;     // 输入端口队列深度（0 表示默认=4）
    int input_stride;    // 输入 stride（0 表示自动计算）
    int output_stride;   // 输出 stride（0 表示自动计算）
    float gain;          // Retinex 增益
    float threshold;     // 阈值
    float log_min;       // 对数最小值
    float log_max;       // 对数最大值
} MEDIA_RETINEX_ATTR;

int MEDIA_RETINEX_CreateGrp(int grp, const MEDIA_RETINEX_ATTR *attr);
int MEDIA_RETINEX_SetGrpAttr(int grp, const MEDIA_RETINEX_ATTR *attr);
int MEDIA_RETINEX_DestroyGrp(int grp);
int MEDIA_RETINEX_Start(int grp);
int MEDIA_RETINEX_Stop(int grp);
int MEDIA_RETINEX_GetFrame(int grp, MEDIA_BUFFER *buf, int timeout_ms);
int MEDIA_RETINEX_ReleaseFrame(int grp, MEDIA_BUFFER buf);


// ThermalColor (热成像颜色映射）
typedef enum {
    MEDIA_THERMAL_COLOR_RAINBOW = 0,      // 彩虹色（蓝→青→绿→黄→红，4 段）
    MEDIA_THERMAL_COLOR_BLACK_HOT = 1,    // 黑热（黑→白）
    MEDIA_THERMAL_COLOR_WHITE_HOT = 2,    // 白热（白→黑）
    MEDIA_THERMAL_COLOR_IRON = 3,         // 铁红（金属氧化色，3 段）
    MEDIA_THERMAL_COLOR_SEPIA = 4,        // 怀旧（棕褐色）
    MEDIA_THERMAL_COLOR_BLUE_RED = 5,     // 蓝红（蓝→红）
    MEDIA_THERMAL_COLOR_GRAYSCALE = 6,    // 灰度（原始）
    MEDIA_THERMAL_COLOR_RAINBOW1 = 7,     // 彩虹色变体 1（黑→蓝→青→绿→黄→品红→白，6 段）
    MEDIA_THERMAL_COLOR_RAINBOW2 = 8,     // 彩虹色变体 2（蓝→青→绿→黄→品红→白，5 段）
    MEDIA_THERMAL_COLOR_RAINBOW3 = 9,     // 彩虹色变体 3（蓝→青→绿→黄→橙→红，5 段）
    MEDIA_THERMAL_COLOR_PSEUDO1 = 10,     // 伪彩色 1（简单算法）
    MEDIA_THERMAL_COLOR_PSEUDO2 = 11,     // 伪彩色 2（4 段细致渐变）
    MEDIA_THERMAL_COLOR_METAL1 = 12,      // 金属色调 1（5 段）
    MEDIA_THERMAL_COLOR_METAL2 = 13,      // 金属色调 2（复杂算法）
    MEDIA_THERMAL_COLOR_ZHOU = 14,        // Zhou 模式（4 段变体）
    MEDIA_THERMAL_COLOR_NING = 15,        // Ning 模式（4 段变体）
} MEDIA_THERMAL_COLOR_MODE;

typedef struct {
    int width;               // 输入分辨率宽
    int height;              // 输入分辨率高
    int format;              // 输入格式（MEDIA_FORMAT_NV12 或 MEDIA_FORMAT_GRAY8）
    int color_mode;          // 颜色模式（MEDIA_THERMAL_COLOR_MODE）
    int input_depth;         // 输入端口队列深度（0 表示默认=4）
    int output_depth;        // 输出端口队列深度（0 表示默认=4）
} MEDIA_THERMAL_ATTR;

int MEDIA_THERMAL_CreateGrp(int grp, const MEDIA_THERMAL_ATTR *attr);
int MEDIA_THERMAL_DestroyGrp(int grp);
int MEDIA_THERMAL_Start(int grp);
int MEDIA_THERMAL_Stop(int grp);
int MEDIA_THERMAL_Process(int grp, MEDIA_BUFFER input, MEDIA_BUFFER *output);
int MEDIA_THERMAL_SetColorMode(int grp, int color_mode);
int MEDIA_THERMAL_SetUvLut(int grp, const uint8_t *uv_lut, size_t size);




// VMix (Vulkan 视频混流模块）
#define MEDIA_VMIX_MAX_INPUTS 16

typedef struct {
    int enabled;              // 是否启用该通道（0=禁用，1=启用）
    int x;                    // X 坐标偏移
    int y;                    // Y 坐标偏移
    int width;                // 目标宽度（0=不缩放）
    int height;               // 目标高度（0=不缩放）
    float alpha;              // Alpha 透明度（0.0-1.0）
    int stride;               // 输入 stride (0 表示按输入格式 16 字节对齐)
    int format;               // 输入格式 (MEDIA_FORMAT_NV12/ARGB8888，0 或 -1 表示使用全局默认格式)
} MEDIA_VMIX_CHANNEL;

typedef struct {
    MEDIA_VMIX_CHANNEL channels[MEDIA_VMIX_MAX_INPUTS];
    int input_count;          // 输入通道数（1-MEDIA_VMIX_MAX_INPUTS）
    int output_width;         // 输出宽度
    int output_height;        // 输出高度
    int format;               // 单路 output0 输出格式和默认输入格式（MEDIA_FORMAT_NV12/ARGB8888）
    int input_depth;          // 输入端口队列深度（0 表示默认=4）
    int output_pool_id;       // 输出 buffer 池 ID
    int output_stride;        // 输出 stride (0 表示按输出格式 16 字节对齐)
    int primary_index;        // 主输入索引 (-1 表示自动选择第一个启用的通道)
} MEDIA_VMIX_ATTR;

int MEDIA_VMIX_SetAttr(int grp, const MEDIA_VMIX_ATTR *attr);
int MEDIA_VMIX_CreateGrp(int grp, const MEDIA_VMIX_ATTR *attr);
int MEDIA_VMIX_DestroyGrp(int grp);
int MEDIA_VMIX_Start(int grp);
int MEDIA_VMIX_Stop(int grp);
int MEDIA_VMIX_Enable(int grp);
int MEDIA_VMIX_Disable(int grp);
int MEDIA_VMIX_GetFrame(int grp, MEDIA_BUFFER *buf, int timeout_ms);
int MEDIA_VMIX_ReleaseFrame(int grp, MEDIA_BUFFER buf);

// CSC RGA (使用 RGA 硬件的颜色空间转换模块）
typedef struct {
    int input_width;            /* 输入宽度 */
    int input_height;           /* 输入高度 */
    int input_format;           /* 输入格式 (MEDIA_FORMAT_NV12, MEDIA_FORMAT_RGB888, MEDIA_FORMAT_RGBA8888) */
    int output_format;          /* 输出格式 (MEDIA_FORMAT_NV12, MEDIA_FORMAT_RGB888, MEDIA_FORMAT_RGBA8888) */
    int input_depth;            /* 输入端口队列深度（0 表示默认=4） */
    int output_pool_id;         /* 输出 buffer 池 ID */
    int input_stride;           /* 输入 stride (0 表示根据格式自动计算) */
    int output_stride;          /* 输出 stride (0 表示根据格式自动计算) */
    int csc_mode;               /* 颜色空间转换模式 (CSC_RGA_CSC_*) */
} MEDIA_CSC_RGA_ATTR;

int MEDIA_CSC_RGA_CreateGrp(int grp, const MEDIA_CSC_RGA_ATTR *attr);
int MEDIA_CSC_RGA_DestroyGrp(int grp);
int MEDIA_CSC_RGA_Start(int grp);
int MEDIA_CSC_RGA_Stop(int grp);
int MEDIA_CSC_RGA_Enable(int grp);
int MEDIA_CSC_RGA_Disable(int grp);
int MEDIA_CSC_RGA_GetFrame(int grp, MEDIA_BUFFER *buf, int timeout_ms);
int MEDIA_CSC_RGA_ReleaseFrame(int grp, MEDIA_BUFFER buf);

// CSC CL (使用 OpenCL GPU 的颜色空间转换模块）
typedef struct {
    int input_width;            /* 输入宽度 */
    int input_height;           /* 输入高度 */
    int input_format;           /* 输入格式 (MEDIA_FORMAT_NV12, MEDIA_FORMAT_RGB888, MEDIA_FORMAT_RGBA8888) */
    int output_format;          /* 输出格式 (MEDIA_FORMAT_NV12, MEDIA_FORMAT_RGB888, MEDIA_FORMAT_RGBA8888) */
    int input_depth;            /* 输入端口队列深度（0 表示默认=4） */
    int output_pool_id;         /* 输出 buffer 池 ID */
    int input_stride;           /* 输入 stride (0 表示默认=根据格式自动计算) */
    int output_stride;          /* 输出 stride (0 表示默认=根据格式自动计算) */
} MEDIA_CSC_CL_ATTR;

typedef struct {
    double gpu_kernel_total_ms;
    double gpu_queue_total_ms;
} MEDIA_CSC_CL_PERF;

int MEDIA_CSC_CL_CreateGrp(int grp, const MEDIA_CSC_CL_ATTR *attr);
int MEDIA_CSC_CL_DestroyGrp(int grp);
int MEDIA_CSC_CL_Start(int grp);
int MEDIA_CSC_CL_Stop(int grp);
int MEDIA_CSC_CL_Enable(int grp);
int MEDIA_CSC_CL_Disable(int grp);
int MEDIA_CSC_CL_SendFrame(int grp, MEDIA_BUFFER buf, int timeout_ms);
int MEDIA_CSC_CL_GetFrame(int grp, MEDIA_BUFFER *buf, int timeout_ms);
int MEDIA_CSC_CL_ReleaseFrame(int grp, MEDIA_BUFFER buf);
int MEDIA_CSC_CL_SetMatrix(int grp, const float *matrix, int matrix_size);
int MEDIA_CSC_CL_GetLastPerf(int grp, MEDIA_CSC_CL_PERF *perf);

// CAP_DEHAZE (使用 OpenCL/CPU 的 CAP 去雾模块)
typedef struct {
    int width;                  /* 图像宽度 */
    int height;                 /* 图像高度 */
    int format;                 /* 图像格式 (MEDIA_FORMAT_RGB888, MEDIA_FORMAT_BGR888) */
    int input_depth;            /* 输入端口队列深度（0 表示默认=4） */
    int output_pool_id;         /* 输出 buffer 池 ID */
    int input_stride;           /* 输入 stride (0 表示默认=根据格式自动计算) */
    int output_stride;          /* 输出 stride (0 表示默认=根据格式自动计算) */
    int guided_radius;          /* 引导滤波半径 */
    float guided_eps;           /* 引导滤波 epsilon */
    float t0;                   /* 最小 transmission */
    float beta0;                /* CAP beta0 */
    float beta1;                /* CAP beta1 */
    float beta2;                /* CAP beta2 */
    float depth_scale;          /* depth 缩放 */
} MEDIA_CAP_DEHAZE_ATTR;

int MEDIA_CAP_DEHAZE_CreateGrp(int grp, const MEDIA_CAP_DEHAZE_ATTR *attr);
int MEDIA_CAP_DEHAZE_DestroyGrp(int grp);
int MEDIA_CAP_DEHAZE_Start(int grp);
int MEDIA_CAP_DEHAZE_Stop(int grp);
int MEDIA_CAP_DEHAZE_Enable(int grp);
int MEDIA_CAP_DEHAZE_Disable(int grp);
int MEDIA_CAP_DEHAZE_SendFrame(int grp, MEDIA_BUFFER buf, int timeout_ms);
int MEDIA_CAP_DEHAZE_GetFrame(int grp, MEDIA_BUFFER *buf, int timeout_ms);
int MEDIA_CAP_DEHAZE_ReleaseFrame(int grp, MEDIA_BUFFER buf);

// DCP_FAST_DEHAZE (使用 OpenCL/CPU 的 DCP_FAST 去雾模块)
typedef struct {
    int width;                  /* 图像宽度 */
    int height;                 /* 图像高度 */
    int format;                 /* 图像格式 (MEDIA_FORMAT_RGB888, MEDIA_FORMAT_BGR888) */
    int input_depth;            /* 输入端口队列深度（0 表示默认=4） */
    int output_pool_id;         /* 输出 buffer 池 ID */
    int input_stride;           /* 输入 stride (0 表示默认=根据格式自动计算) */
    int output_stride;          /* 输出 stride (0 表示默认=根据格式自动计算) */
    int patch;                  /* dark channel patch 尺寸 */
    float omega;                /* haze 保留系数 */
    float t0;                   /* 最小 transmission */
    float airlight_percent;     /* airlight 百分位 */
    int guided_radius;          /* 引导滤波半径 */
    float guided_eps;           /* 引导滤波 epsilon */
    float refine_scale;         /* transmission refine 缩放 */
} MEDIA_DCP_FAST_DEHAZE_ATTR;

int MEDIA_DCP_FAST_DEHAZE_CreateGrp(int grp, const MEDIA_DCP_FAST_DEHAZE_ATTR *attr);
int MEDIA_DCP_FAST_DEHAZE_DestroyGrp(int grp);
int MEDIA_DCP_FAST_DEHAZE_Start(int grp);
int MEDIA_DCP_FAST_DEHAZE_Stop(int grp);
int MEDIA_DCP_FAST_DEHAZE_Enable(int grp);
int MEDIA_DCP_FAST_DEHAZE_Disable(int grp);
int MEDIA_DCP_FAST_DEHAZE_SendFrame(int grp, MEDIA_BUFFER buf, int timeout_ms);
int MEDIA_DCP_FAST_DEHAZE_GetFrame(int grp, MEDIA_BUFFER *buf, int timeout_ms);
int MEDIA_DCP_FAST_DEHAZE_ReleaseFrame(int grp, MEDIA_BUFFER buf);

// CONV_CL (使用 OpenCL GPU 的卷积模块）
typedef struct {
    int width;                  /* 图像宽度 */
    int height;                 /* 图像高度 */
    int format;                 /* 图像格式 (MEDIA_FORMAT_RGB888, MEDIA_FORMAT_RGBA8888) */
    int kernel_size;            /* 卷积核大小 (默认=5) */
    int input_depth;            /* 输入端口队列深度（0 表示默认=4） */
    int output_pool_id;         /* 输出 buffer 池 ID */
    int input_stride;           /* 输入 stride (0 表示默认=根据格式自动计算) */
    int output_stride;          /* 输出 stride (0 表示默认=根据格式自动计算) */
} MEDIA_CONV_CL_ATTR;

typedef struct {
    double gpu_kernel_total_ms;
    double gpu_queue_total_ms;
} MEDIA_CONV_CL_PERF;

int MEDIA_CONV_CL_CreateGrp(int grp, const MEDIA_CONV_CL_ATTR *attr);
int MEDIA_CONV_CL_DestroyGrp(int grp);
int MEDIA_CONV_CL_Start(int grp);
int MEDIA_CONV_CL_Stop(int grp);
int MEDIA_CONV_CL_GetFrame(int grp, MEDIA_BUFFER *buf, int timeout_ms);
int MEDIA_CONV_CL_ReleaseFrame(int grp, MEDIA_BUFFER buf);
int MEDIA_CONV_CL_SetKernelSize(int grp, int kernel_size);
int MEDIA_CONV_CL_SetTable(int grp, const float *table, int table_size);
int MEDIA_CONV_CL_SetPassthrough(int grp, int enable);
int MEDIA_CONV_CL_GetLastPerf(int grp, MEDIA_CONV_CL_PERF *perf);

// CLAHE (使用 OpenCL GPU 的局部对比度增强模块)
typedef struct {
    int width;
    int height;
    int format;
    int tile_grid_x;
    int tile_grid_y;
    int bins;
    int input_depth;
    int output_pool_id;
    int input_stride;
    int output_stride;
    float clip_limit;
    float highlight_protect_start;
    float highlight_protect_strength;
} MEDIA_CLAHE_ATTR;

int MEDIA_CLAHE_CreateGrp(int grp, const MEDIA_CLAHE_ATTR *attr);
int MEDIA_CLAHE_DestroyGrp(int grp);
int MEDIA_CLAHE_Start(int grp);
int MEDIA_CLAHE_Stop(int grp);
int MEDIA_CLAHE_Enable(int grp);
int MEDIA_CLAHE_Disable(int grp);
int MEDIA_CLAHE_GetFrame(int grp, MEDIA_BUFFER *buf, int timeout_ms);
int MEDIA_CLAHE_ReleaseFrame(int grp, MEDIA_BUFFER buf);
int MEDIA_CLAHE_SetClipLimit(int grp, float clip_limit);
int MEDIA_CLAHE_SetHighlightProtect(int grp, float start, float strength);

// EDOF_CL (使用 OpenCL GPU 的双输入扩景深融合模块)
typedef struct {
    int width;
    int height;
    int format;
    int focus_radius;
    int input_depth;
    int output_pool_id;
    int input_stride;
    int output_stride;
    float score_eps;
} MEDIA_EDOF_CL_ATTR;

typedef struct {
    double gpu_kernel_total_ms;
    double gpu_queue_total_ms;
} MEDIA_EDOF_CL_PERF;

int MEDIA_EDOF_CL_CreateGrp(int grp, const MEDIA_EDOF_CL_ATTR *attr);
int MEDIA_EDOF_CL_DestroyGrp(int grp);
int MEDIA_EDOF_CL_Start(int grp);
int MEDIA_EDOF_CL_Stop(int grp);
int MEDIA_EDOF_CL_Enable(int grp);
int MEDIA_EDOF_CL_Disable(int grp);
int MEDIA_EDOF_CL_GetFrame(int grp, MEDIA_BUFFER *buf, int timeout_ms);
int MEDIA_EDOF_CL_ReleaseFrame(int grp, MEDIA_BUFFER buf);
int MEDIA_EDOF_CL_SetAffineWarp(int grp, float matrix[6]);
int MEDIA_EDOF_CL_GetLastPerf(int grp, MEDIA_EDOF_CL_PERF *perf);

typedef enum {
    MEDIA_STEREO_3D_MODE_SIDE_BY_SIDE = 0,
    MEDIA_STEREO_3D_MODE_LINE_BY_LINE = 1,
} MEDIA_STEREO_3D_MODE;

typedef struct {
    int width;
    int height;
    int format;
    int input_depth;
    int output_pool_id;
    int input_stride;
    int output_stride;
    int mode;
    int rotate_input;
    float rotation_degrees;
} MEDIA_STEREO_3D_ATTR;

int MEDIA_STEREO_3D_CreateGrp(int grp, const MEDIA_STEREO_3D_ATTR *attr);
int MEDIA_STEREO_3D_DestroyGrp(int grp);
int MEDIA_STEREO_3D_Start(int grp);
int MEDIA_STEREO_3D_Stop(int grp);
int MEDIA_STEREO_3D_Enable(int grp);
int MEDIA_STEREO_3D_Disable(int grp);
int MEDIA_STEREO_3D_GetFrame(int grp, MEDIA_BUFFER *buf, int timeout_ms);
int MEDIA_STEREO_3D_ReleaseFrame(int grp, MEDIA_BUFFER buf);
int MEDIA_STEREO_3D_SetRotation(int grp, float rotation_degrees);

// EXPOSURE_FUSION_CL (OpenCL GPU dual-input exposure fusion, conservative realtime route)
typedef struct {
    int width;
    int height;
    int format;
    int input_depth;
    int output_pool_id;
    int input_stride;
    int output_stride;
    float contrast_power;
    float saturation_power;
    float exposedness_power;
    float sigma;
    float epsilon;
} MEDIA_EXPOSURE_FUSION_CL_ATTR;

int MEDIA_EXPOSURE_FUSION_CL_CreateGrp(int grp, const MEDIA_EXPOSURE_FUSION_CL_ATTR *attr);
int MEDIA_EXPOSURE_FUSION_CL_DestroyGrp(int grp);
int MEDIA_EXPOSURE_FUSION_CL_Start(int grp);
int MEDIA_EXPOSURE_FUSION_CL_Stop(int grp);
int MEDIA_EXPOSURE_FUSION_CL_Enable(int grp);
int MEDIA_EXPOSURE_FUSION_CL_Disable(int grp);
int MEDIA_EXPOSURE_FUSION_CL_GetFrame(int grp, MEDIA_BUFFER *buf, int timeout_ms);
int MEDIA_EXPOSURE_FUSION_CL_ReleaseFrame(int grp, MEDIA_BUFFER buf);

// OSD (干净的多 region overlay 模块)
#define MEDIA_OSD_CONTENT_NONE     0
#define MEDIA_OSD_CONTENT_RECT     1
#define MEDIA_OSD_CONTENT_A8_MASK  2
#define MEDIA_OSD_CONTENT_ARGB8888 3
#define MEDIA_OSD_MAX_REGIONS      64

typedef struct {
    uint8_t r;
    uint8_t g;
    uint8_t b;
    uint8_t a;
} MEDIA_OSD_COLOR;

typedef struct {
    int enabled;
    int x;
    int y;
    int width;
    int height;
    int zorder;
    uint8_t global_alpha;
} MEDIA_OSD_REGION_ATTR;

typedef struct {
    int filled;
    int line_width;
    MEDIA_OSD_COLOR color;
} MEDIA_OSD_RECT_DESC;

typedef struct {
    int width;
    int height;
    int stride;
    const uint8_t *data;
    size_t data_size;
    MEDIA_OSD_COLOR color;
} MEDIA_OSD_MASK_DESC;

typedef struct {
    int input_width;
    int input_height;
    int format;
    int input_depth;
    int output_pool_id;
    int input_stride;
    int output_stride;
    int max_regions;
} MEDIA_OSD_ATTR;

int MEDIA_OSD_CreateGrp(int grp, const MEDIA_OSD_ATTR *attr);
int MEDIA_OSD_DestroyGrp(int grp);
int MEDIA_OSD_Start(int grp);
int MEDIA_OSD_Stop(int grp);
int MEDIA_OSD_Enable(int grp);
int MEDIA_OSD_Disable(int grp);
int MEDIA_OSD_SendFrame(int grp, MEDIA_BUFFER buf, int timeout_ms);
int MEDIA_OSD_GetFrame(int grp, MEDIA_BUFFER *buf, int timeout_ms);
int MEDIA_OSD_ReleaseFrame(int grp, MEDIA_BUFFER buf);
int MEDIA_OSD_UpdateRegion(int grp, int region_id, const MEDIA_OSD_REGION_ATTR *attr);
int MEDIA_OSD_SetRegionRect(int grp, int region_id, const MEDIA_OSD_RECT_DESC *rect);
int MEDIA_OSD_SetRegionMask(int grp, int region_id, const MEDIA_OSD_MASK_DESC *mask);

// VMix RGA (使用 RGA 硬件的视频合成模块）
#define MEDIA_VMIX_RGA_MAX_INPUTS 16

/* VMix RGA 通道配置 */
typedef struct {
    int enabled;                /* 是否启用该通道 */
    int x;                      /* X 坐标偏移 */
    int y;                      /* Y 坐标偏移 */
    int width;                  /* 目标宽度 (0 表示不缩放) */
    int height;                 /* 目标高度 (0 表示不缩放) */
    int stride;                 /* 输入 stride (0 表示默认=width) */
    int format;                 /* 输入格式 (MEDIA_FORMAT_NV12, MEDIA_FORMAT_RGB888, MEDIA_FORMAT_RGBA8888)，-1 表示使用全局格式 */
} MEDIA_VMIX_RGA_CHANNEL;

/* VMix RGA 属性 */
typedef struct {
    MEDIA_VMIX_RGA_CHANNEL channels[MEDIA_VMIX_RGA_MAX_INPUTS];  /* 通道配置 */
    int input_count;            /* 输入通道数 (1-16) */
    int output_width;           /* 输出宽度 */
    int output_height;          /* 输出高度 */
    int format;                 /* 格式 */
    int input_depth;            /* 输入端口队列深度（0 表示默认=4） */
    int output_pool_id;         /* 输出 buffer 池 ID */
    int output_stride;          /* 输出 stride (0 表示默认=output_width) */
    int primary_index;          /* 主输入索引 (-1 表示自动选择第一个启用的通道) */
    int use_rga_compose;        /* 使用 RGA compose 算法 (0=逐个 blit, 1=使用 imcomposite) */
} MEDIA_VMIX_RGA_ATTR;

int MEDIA_VMIX_RGA_CreateGrp(int grp, const MEDIA_VMIX_RGA_ATTR *attr);
int MEDIA_VMIX_RGA_DestroyGrp(int grp);
int MEDIA_VMIX_RGA_Start(int grp);
int MEDIA_VMIX_RGA_Stop(int grp);
int MEDIA_VMIX_RGA_Enable(int grp);
int MEDIA_VMIX_RGA_Disable(int grp);
int MEDIA_VMIX_RGA_GetFrame(int grp, MEDIA_BUFFER *buf, int timeout_ms);
int MEDIA_VMIX_RGA_ReleaseFrame(int grp, MEDIA_BUFFER buf);

// Resize RGA (使用 RGA 硬件的缩放模块）
/* Resize RGA 属性 */
typedef struct {
    // 输入参数
    int src_x;                  /* 源图像 X 坐标裁剪起点 */
    int src_y;                  /* 源图像 Y 坐标裁剪起点 */
    int src_width;              /* 源图像裁剪宽度 (0 表示使用输入端口宽度) */
    int src_height;             /* 源图像裁剪高度 (0 表示使用输入端口高度) */
    int input_stride;           /* 输入 stride (0 表示默认=width) */
    int input_format;           /* 输入格式 (MEDIA_FORMAT_NV12, MEDIA_FORMAT_RGB888, MEDIA_FORMAT_RGBA8888) */
    int input_depth;            /* 输入端口队列深度（0 表示默认=4） */
    
    // 输出参数
    int out_width;              /* 输出宽度 */
    int out_height;             /* 输出高度 */
    int out_stride;             /* 输出 stride (0 表示默认=out_width) */
    int output_format;          /* 输出格式 (MEDIA_FORMAT_NV12, MEDIA_FORMAT_RGB888, MEDIA_FORMAT_RGBA8888) */
    int output_pool_id;         /* 输出 buffer 池 ID */
} MEDIA_RESIZE_RGA_ATTR;

int MEDIA_RESIZE_RGA_CreateGrp(int grp, const MEDIA_RESIZE_RGA_ATTR *attr);
int MEDIA_RESIZE_RGA_DestroyGrp(int grp);
int MEDIA_RESIZE_RGA_Start(int grp);
int MEDIA_RESIZE_RGA_Stop(int grp);
int MEDIA_RESIZE_RGA_Enable(int grp);
int MEDIA_RESIZE_RGA_Disable(int grp);
int MEDIA_RESIZE_RGA_GetFrame(int grp, MEDIA_BUFFER *buf, int timeout_ms);
int MEDIA_RESIZE_RGA_ReleaseFrame(int grp, MEDIA_BUFFER buf);

// DualView (双摄图像合并模块）
typedef enum {
    MEDIA_DUALVIEW_MODE_SIDE_BY_SIDE = 0,   /* 并排显示 (左右) */
    MEDIA_DUALVIEW_MODE_LINE_BY_LINE = 1,    /* 逐行交错 */
} MEDIA_DUALVIEW_MODE;

/* DualView 输入通道配置 */
typedef struct {
    int enabled;        /* 是否启用该路输入 */
    int x;              /* 输出 X 坐标偏移 (side by side 模式使用) */
    int y;              /* 输出 Y 坐标偏移 */
    int width;          /* 输出宽度 (0 表示使用输入宽度) */
    int height;         /* 输出高度 (0 表示使用输入高度) */
    int stride;         /* 输入 stride (0 表示使用 width*3) */
} MEDIA_DUALVIEW_INPUT;

typedef struct {
    MEDIA_DUALVIEW_INPUT inputs[2];  /* 两路输入配置 */
    int input_width;            /* 输入宽度 */
    int input_height;           /* 输入高度 */
    int input_stride;           /* 输入 stride (0 表示 width*3) */
    int output_width;           /* 输出宽度（<=0 表示按模式自动） */
    int output_height;          /* 输出高度（<=0 表示按模式自动） */
    int output_stride;          /* 输出 stride (0 表示 width*3) */
    int mode;                   /* 合并模式（MEDIA_DUALVIEW_MODE） */
    int format;                 /* 格式（MEDIA_FORMAT_RGB888） */
    int input_depth;            /* 输入端口队列深度（0 表示默认=4） */
    int output_pool_id;         /* 输出 buffer 池 ID */
} MEDIA_DUALVIEW_ATTR;

int MEDIA_DUALVIEW_CreateGrp(int grp, const MEDIA_DUALVIEW_ATTR *attr);
int MEDIA_DUALVIEW_DestroyGrp(int grp);
int MEDIA_DUALVIEW_Start(int grp);
int MEDIA_DUALVIEW_Stop(int grp);
int MEDIA_DUALVIEW_GetFrame(int grp, MEDIA_BUFFER *buf, int timeout_ms);
int MEDIA_DUALVIEW_ReleaseFrame(int grp, MEDIA_BUFFER buf);

// Transform (基于 LUT 的 XY 变换模块，支持 NV12 与 RGB/BGR/RGBA family 同格式 remap）
typedef struct {
    int out_width;           // 输出分辨率宽
    int out_height;          // 输出分辨率高
    int out_stride;          // 输出 stride 字节数（0=按格式自动计算并 4 字节对齐）
    int format;              // 输入/输出格式（NV12/RGB888/BGR888/RGBA8888/BGRA8888/ARGB8888/ABGR8888）
    int input_depth;         // 输入端口队列深度（0 表示默认=4）
    int pool_id;             // 输出缓冲池 ID
    int in_width;            // 输入分辨率宽（旋转等变换时与输出不同，0=等于 out_width）
    int in_height;           // 输入分辨率高（旋转等变换时与输出不同，0=等于 out_height）
    int in_stride;           // 输入 stride 字节数（0=按格式自动计算并 4 字节对齐）
    int lut_width;           // LUT 宽度（0=等于 out_width，可小于 out_width 实现下采样节省内存）
    int lut_height;          // LUT 高度（0=等于 out_height，可小于 out_height 实现下采样节省内存）
    const float *lut;        // 预计算的 LUT（大小为 lut_width*lut_height*2）
    size_t lut_size;         // LUT 大小（字节）
} MEDIA_TRANSFORM_ATTR;

int MEDIA_TRANSFORM_CreateGrp(int grp, const MEDIA_TRANSFORM_ATTR *attr);
int MEDIA_TRANSFORM_DestroyGrp(int grp);
int MEDIA_TRANSFORM_Start(int grp);
int MEDIA_TRANSFORM_Stop(int grp);
int MEDIA_TRANSFORM_Process(int grp, MEDIA_BUFFER input, MEDIA_BUFFER *output);
int MEDIA_TRANSFORM_GetFrame(int grp, MEDIA_BUFFER *buf, int timeout_ms);
int MEDIA_TRANSFORM_ReleaseFrame(int grp, MEDIA_BUFFER buf);
int MEDIA_TRANSFORM_UpdateLut(int grp, const float *lut, size_t lut_size);

// Panorama stitch (online multi-input module, PTO-only)
typedef struct {
    int input_count; // 2..8
    int in_width;
    int in_height;
    int in_stride;   // 输入 NV12 stride（0=默认=in_width）
    int out_width;
    int out_height;  // NV12 needs even height; odd will be rounded up internally
    int out_stride;  // 输出 NV12 stride（0=默认按内部对齐）
    int crop_enable; // 1=启用 PTO 全景域裁剪窗口映射到输出（crop_rect -> out_size）
    int crop_x;      // 裁剪窗口左上角 X（PTO 全景域坐标）
    int crop_y;      // 裁剪窗口左上角 Y（PTO 全景域坐标）
    int crop_width;  // 裁剪窗口宽（PTO 全景域尺寸，0=默认域剩余宽度）
    int crop_height; // 裁剪窗口高（PTO 全景域尺寸，0=默认域剩余高度）
    int output_pool_id; // 在线模块输出 buffer 池 ID（离线路径忽略）
    int input_depth;    // 输入端口队列深度（0=默认）
    int output_depth;   // 输出端口队列深度（0=默认）
    int sync_timeout_ms; // 多路聚合同步超时（<=0 使用默认）
    int lut_width;      // LUT 宽度（0=默认使用 out_width；crop+pto 自动模式下默认使用 PTO 全景宽）
    int lut_height;     // LUT 高度（0=默认使用 out_height；crop+pto 自动模式下默认使用 PTO 全景高）
    const char *pto_path; // required: calibration file path (.pto/.pts), used to build LUT and stitch
} MEDIA_PANO_ATTR;

int MEDIA_PANO_CreateGrp(int grp, const MEDIA_PANO_ATTR *attr);
int MEDIA_PANO_DestroyGrp(int grp);
int MEDIA_PANO_Start(int grp);
int MEDIA_PANO_Stop(int grp);
int MEDIA_PANO_Enable(int grp);
int MEDIA_PANO_Disable(int grp);
int MEDIA_PANO_SetCrop(int grp, int crop_x, int crop_y); // runtime update crop origin (keeps crop_width/height)
int MEDIA_PANO_SendFrame(int grp, int input_id, MEDIA_BUFFER buf, int timeout_ms);
int MEDIA_PANO_GetFrame(int grp, MEDIA_BUFFER *buf, int timeout_ms);
int MEDIA_PANO_ReleaseFrame(int grp, MEDIA_BUFFER buf);

// AVM 2D runtime (online multi-input module, precomputed LUT-only)
typedef struct {
    int input_count;       // 2..8, typical AVM uses 4
    int in_width;
    int in_height;
    int in_stride;         // input NV12 stride (0 = in_width)
    int out_width;
    int out_height;
    int out_stride;        // output NV12 stride (0 = backend default)
    int output_pool_id;    // required output pool id
    int input_depth;       // input queue depth (0 = default)
    int output_depth;      // reserved for symmetry
    int sync_timeout_ms;   // multi-input sync timeout (<=0 uses default)
    int lut_width;         // LUT width (0 = out_width)
    int lut_height;        // LUT height (0 = out_height)
    const char *lut_path;  // required precomputed LUT file
} MEDIA_AVM_ATTR;

int MEDIA_AVM_CreateGrp(int grp, const MEDIA_AVM_ATTR *attr);
int MEDIA_AVM_DestroyGrp(int grp);
int MEDIA_AVM_Start(int grp);
int MEDIA_AVM_Stop(int grp);
int MEDIA_AVM_Enable(int grp);
int MEDIA_AVM_Disable(int grp);
int MEDIA_AVM_SendFrame(int grp, int input_id, MEDIA_BUFFER buf, int timeout_ms);
int MEDIA_AVM_GetFrame(int grp, MEDIA_BUFFER *buf, int timeout_ms);
int MEDIA_AVM_ReleaseFrame(int grp, MEDIA_BUFFER buf);

// SVM 3D runtime (online multi-input module, external mesh/mask asset manifest)
typedef struct {
    int input_count;       // 2..8, typical surround-view pipeline uses 4
    int input_format;      // input format, current demo assets assume NV12 camera input
    int in_width;
    int in_height;
    int in_stride;         // input stride (0 = format default)
    int output_format;     // output format, runtime target is RGB888 but skeleton also supports NV12 passthrough
    int out_width;
    int out_height;
    int out_stride;        // output stride (0 = format default)
    int output_pool_id;    // required output pool id
    int input_depth;       // input queue depth (0 = default)
    int output_depth;      // reserved for symmetry
    int sync_timeout_ms;   // multi-input sync timeout (<=0 uses default)
    const char *asset_path; // required svm_3d asset manifest path
} MEDIA_SVM3D_ATTR;

typedef struct {
    float yaw_deg;
    float pitch_deg;
    float distance_m;
    float target_z_m;
    float fov_y_deg;
} MEDIA_SVM3D_VIEW_ATTR;

int MEDIA_SVM3D_CreateGrp(int grp, const MEDIA_SVM3D_ATTR *attr);
int MEDIA_SVM3D_DestroyGrp(int grp);
int MEDIA_SVM3D_Start(int grp);
int MEDIA_SVM3D_Stop(int grp);
int MEDIA_SVM3D_Enable(int grp);
int MEDIA_SVM3D_Disable(int grp);
int MEDIA_SVM3D_SendFrame(int grp, int input_id, MEDIA_BUFFER buf, int timeout_ms);
int MEDIA_SVM3D_GetFrame(int grp, MEDIA_BUFFER *buf, int timeout_ms);
int MEDIA_SVM3D_ReleaseFrame(int grp, MEDIA_BUFFER buf);
int MEDIA_SVM3D_SetView(int grp, const MEDIA_SVM3D_VIEW_ATTR *attr);
int MEDIA_SVM3D_GetView(int grp, MEDIA_SVM3D_VIEW_ATTR *attr);

// PIC_SEND (image folder -> video stream)
int MEDIA_PIC_SEND_SetAttr(int chn, const MEDIA_PIC_SEND_ATTR *attr);
int MEDIA_PIC_SEND_Enable(int chn);
int MEDIA_PIC_SEND_Disable(int chn);

// PIC_REV (video stream -> image folder)
int MEDIA_PIC_REV_SetAttr(int chn, const MEDIA_PIC_REV_ATTR *attr);
int MEDIA_PIC_REV_Enable(int chn);
int MEDIA_PIC_REV_Disable(int chn);



// VO
typedef enum {
    MEDIA_VO_INTF_HDMI = 0,
    MEDIA_VO_INTF_DP = 1,
    MEDIA_VO_INTF_EDP = 2,
    MEDIA_VO_INTF_MIPI = 3,
} MEDIA_VO_INTF;

typedef enum {
    MEDIA_VO_PLANE_TYPE_AUTO = 0,     // 自动选择 (overlay 优先，无 overlay 则用 primary)
    MEDIA_VO_PLANE_TYPE_OVERLAY = 1,  // 必须是 overlay plane
    MEDIA_VO_PLANE_TYPE_PRIMARY = 2,  // 必须是 primary plane
} MEDIA_VO_PLANE_TYPE;

typedef struct {
    MEDIA_VO_INTF intf;
    int width;
    int height;
    int plane_count;
} MEDIA_VO_ATTR;

typedef struct {
    const char *device;     // DRM card，NULL 表示 /dev/dri/card0
    const char *target;     // 显示 connector 名称，如 DSI-1 / HDMI-A-1；NULL 表示自动找 active CRTC
    int connector_id;       // writeback connector id，<=0 自动查找
    int crtc_id;            // 被回环的 CRTC id，<=0 自动使用当前 active CRTC
    int width;
    int height;
    int stride;             // bytesperline，<=0 自动
    int fps;                // <=0 不做帧率限制
    int format;             // MEDIA_FORMAT_NV12 / BGR888 / ARGB8888
    int pool_id;            // 输出 pool id
    int output_depth;       // 输出端口队列深度，<=0 默认 4
} MEDIA_VO_WBC_ATTR;

int MEDIA_VO_SetAttr(int output_id, const MEDIA_VO_ATTR *attr);
int MEDIA_VO_GetOutput(int output_id, MEDIA_VO_INTF *intf, int *width, int *height, int *plane_count);
int MEDIA_VO_CreateChn(int output_id, int chn, int x, int y, int width, int height, int stride, int depth,
                       MEDIA_VO_PLANE_TYPE plane_type, int format);
int MEDIA_VO_DestroyChn(int output_id, int chn);
int MEDIA_VO_Start(int output_id, int chn);
int MEDIA_VO_Stop(int output_id, int chn);
int MEDIA_VO_GetFrame(int output_id, int chn, MEDIA_BUFFER *buf, int timeout_ms);
int MEDIA_VO_ReleaseFrame(int output_id, int chn, MEDIA_BUFFER buf);
int MEDIA_VO_FreezeMain(int freeze);
int MEDIA_VO_UnfreezeMain(void);
int MEDIA_VO_FreezePlane(int output_id, int plane_id, int freeze);
int MEDIA_VO_HidePlane(int output_id, int plane_id, int hide);

int MEDIA_VO_WBC_Probe(const char *device, int *connector_id, int *crtc_id);
int MEDIA_VO_WBC_CreateChn(int chn, const MEDIA_VO_WBC_ATTR *attr);
int MEDIA_VO_WBC_DestroyChn(int chn);
int MEDIA_VO_WBC_Start(int chn);
int MEDIA_VO_WBC_Stop(int chn);
int MEDIA_VO_WBC_GetFrame(int chn, MEDIA_BUFFER *buf, int timeout_ms);
int MEDIA_VO_WBC_ReleaseFrame(int chn, MEDIA_BUFFER buf);

// 手动发送
int MEDIA_SYS_SendFrame(const char *dst_mod, int dst_id, const char *dst_port,
                        MEDIA_BUFFER buf, int timeout_ms);

// All Debug API
// debug_buf由调用者提供；返回0表示完整写入，1表示被截断，-1表示参数或状态错误。
int MEDIA_SYS_GetDebugString(char *debug_buf, size_t debug_buf_size);
int MEDIA_SYS_debug(void);

#endif // MEDIA_API_H
