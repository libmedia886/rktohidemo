# RKTohi AllDemo

独立总控 demo 工程，只拷贝 `media_api.h`、`libmedia.so` 和必要素材，不拷贝 `rktohi/src` 或原 demo 源码。

## 许可与客户交付

本工程包含 `libmedia` 头文件和二进制库，客户交付包必须随附并保留根目录下的 `LICENSE.TXT`。

客户在安装、运行、测试、集成、部署或继续使用本工程及其二进制库前，应阅读并遵守 `LICENSE.TXT`。商业用途必须事先取得权利人书面商业授权并支付许可费用；未经书面许可，不得逆向工程、反编译、反汇编、破解、绕过授权校验、删除许可/版权标识，或向第三方分发、出租、出售、出借二进制库。

正式对外交付时，应在合同、订单、交付确认、验收单或其他书面文件中明确引用 `LICENSE.TXT` 作为交付许可条款；违约与赔偿责任以 `LICENSE.TXT` 第 5 条及双方书面合同约定为准。

## 构建

```bash
cd /userdata/alldemo
cmake -S . -B build
cmake --build build -j
```

## 运行

```bash
/userdata/alldemo/scripts/run_alldemo.sh
```

默认参数适配当前设备，并面向客户演示：

- 屏幕：DSI/MIPI 竖屏 `1080x1920`
- 摄像头：`/dev/video-camera0`
- 主画面：默认轮播客户演示页，屏幕上显示当前模块名、页码、实测 FPS、帧耗时、CPU/GPU/RGA 占用
- 重算法或需要专门输入的模块：用 `--only <tile>` 单页展示，避免默认轮播转到占位页

按 `Ctrl+C` 退出。

如果只想固定在第一个功能模块屏：

```bash
/userdata/alldemo/scripts/run_alldemo.sh --no-rotate-main
```

默认客户演示分页策略：轮播用户容易看懂、画面效果明显、稳定上屏的页面：`VI`、`VPSS`、`VMIX`、`OSD`、`RGA`、`RESIZE_RGA`、`CSC_CL`、`CLAHE`、`RETINEX`、`RETINEX_OFFLINE`、`TNR_CL`、`WAVELET_NR_CL`、`HIGHLIGHT_SUPPRESS_VI`、`CAP_DEHAZE`、`CAP_DEHAZE_OFFLINE`、`CONV_CL`、`TRANSFORM`、`THERMAL`、`THERMAL_LOWLIGHT_FUSION_CL`、`EDOF_CL`、`MCF_FUSION_CL`、`PANO`。偏工程验证或调试意味更强的 `VO`、`WBC`、`CSC_RGA`、`STEREO_3D` 保留在工程演示或 `--only` 模式，避免客户现场误解为黑屏、冻结、格式细节或调试页。

如果要看完整工程页表：

```bash
/userdata/alldemo/scripts/run_alldemo.sh --engineering-demo
```

客户演示模式也可以显式指定：

```bash
/userdata/alldemo/scripts/run_alldemo.sh --customer-demo
```

默认运行先按时间自动循环；如果按一次音量键就切到手动模式，不再自动翻页。`KEY_VOLUMEUP` 切到下一页，`KEY_VOLUMEDOWN` 切到上一页。

如果要逐个验证某个小窗体，不让其它实时算法一起运行：

```bash
/userdata/alldemo/scripts/run_alldemo.sh --only VPSS
/userdata/alldemo/scripts/run_alldemo.sh --only VI
/userdata/alldemo/scripts/run_alldemo.sh --only VO
/userdata/alldemo/scripts/run_alldemo.sh --only WBC
/userdata/alldemo/scripts/run_alldemo.sh --only OSD
/userdata/alldemo/scripts/run_alldemo.sh --only RGA
/userdata/alldemo/scripts/run_alldemo.sh --only RESIZE_RGA
/userdata/alldemo/scripts/run_alldemo.sh --only CSC_RGA
/userdata/alldemo/scripts/run_alldemo.sh --only CSC_CL
/userdata/alldemo/scripts/run_alldemo.sh --only TRANSFORM
/userdata/alldemo/scripts/run_alldemo.sh --only CAP_DEHAZE
/userdata/alldemo/scripts/run_alldemo.sh --only CAP_DEHAZE_OFFLINE
/userdata/alldemo/scripts/run_alldemo.sh --only DCP_FAST_DEHAZE
/userdata/alldemo/scripts/run_alldemo.sh --only THERMAL
/userdata/alldemo/scripts/run_alldemo.sh --only THERMAL_LOWLIGHT_FUSION_CL
/userdata/alldemo/scripts/run_alldemo.sh --only CONV_CL
/userdata/alldemo/scripts/run_alldemo.sh --only VMIX
/userdata/alldemo/scripts/run_alldemo.sh --only CLAHE
/userdata/alldemo/scripts/run_alldemo.sh --only RETINEX
/userdata/alldemo/scripts/run_alldemo.sh --only RETINEX_OFFLINE
/userdata/alldemo/scripts/run_alldemo.sh --only EIS
/userdata/alldemo/scripts/run_alldemo.sh --only EIS_VI
/userdata/alldemo/scripts/run_alldemo.sh --only EIS_DETECT_NPU
/userdata/alldemo/scripts/run_alldemo.sh --only FRUIT_DETECT_NPU
/userdata/alldemo/scripts/run_alldemo.sh --only TNR_CL
/userdata/alldemo/scripts/run_alldemo.sh --only WAVELET_NR_CL
/userdata/alldemo/scripts/run_alldemo.sh --only HIGHLIGHT_SUPPRESS
/userdata/alldemo/scripts/run_alldemo.sh --only HIGHLIGHT_SUPPRESS_VI
/userdata/alldemo/scripts/run_alldemo.sh --only EDOF_CL
/userdata/alldemo/scripts/run_alldemo.sh --only MCF_FUSION_CL
/userdata/alldemo/scripts/run_alldemo.sh --only DUALVIEW
/userdata/alldemo/scripts/run_alldemo.sh --only STEREO_3D
/userdata/alldemo/scripts/run_alldemo.sh --only PANO
```

`--only <tile>` 会把主画面固定到指定小窗体，并且只初始化该小窗体需要的实时模块；未接入真实实时链路的 tile 会显示循环素材或合成占位，且不会强制打开摄像头。
`TRANSFORM` 单独模式使用 3840x2160 实时摄像头输入，走 `VI 3840x2160 -> VPSS(4路同源480x480 NV12) -> RAW/3x TRANSFORM -> VMIX -> OSD -> VO` bind 链路，同屏展示 RAW、UNDISTORT、ROTATE ZOOM、PERSPECTIVE 四路画面；ROTATE ZOOM 路实时更新 XY LUT，VMIX 后的 OSD 层显示中文数据流、四路帧计数、LUT 更新序号和 CPU/GPU/RGA 占用率，并保存 `vo_captures/transform_vo_*.bmp` 供复看。
`VO` 单独模式不占用摄像头，页面直接展示 MIPI/DSI 输出、1080x1920、NV12 plane、动态扫描条和彩条。
`VO` 单独模式会动态调用 `MEDIA_VO_FreezeMain`、`MEDIA_VO_FreezePlane` 和 `MEDIA_VO_HidePlane`，展示正常刷新、主显示冻结、plane 冻结、短暂隐藏再恢复。隐藏只持续约 1 秒，退出时会强制恢复显示。
`WBC` 单独模式使用实时摄像头和 DRM writeback，走 `VI -> RESIZE_RGA -> VMIX`、`VO_WBC -> RESIZE_RGA -> VMIX -> OSD -> VO` bind 链路；左侧显示 VI 缩放后的实时参考窗，右侧显示从当前 CRTC 回抓后缩放的 WBC 窗口，屏幕上显示中文数据流、writeback connector/crtc、模块帧计数和 CPU/GPU/RGA 指标。`VO_WBC` 输出为 NV12，抓取宽度按 64 字节对齐要求使用 `1024x1920`，显示前缩放到 `480x900`，缩放后 stride 为 `512`；VI 显示前缩放到 `480x480`，stride 为 `512`。
`OSD` 单独模式使用 3840x2160 实时摄像头输入，走 `VI 3840x2160 -> RESIZE_RGA 1080x608 -> OSD -> VMIX -> OSD -> VO` bind 链路；单个实时主画面宽度统一为 1080，第一级 live OSD 在等比例缩放后的工作帧上叠加动态目标框、扫描线、状态条和告警块，第二级页面 OSD 叠加中文数据流、参数说明和运行指标。页面展示 region 坐标、zorder 层级、alpha 透明度、enabled 告警开关和两级 OSD 帧计数，并保存 `vo_captures/osd_vo_*.bmp` 供复看。
`RGA` 单独模式使用 3840x2160 实时摄像头输入，走 `VI 3840x2160 -> RGA 1080x608 -> VMIX -> OSD -> VO` bind 链路，单个实时主画面宽度统一为 1080；页面会动态轮播 COPY、移动 CROP+SCALE、水平/垂直翻转和 90/180/270 度旋转；屏幕显示当前 RGA 硬件操作、真实数据流、RGA 帧计数和 CPU/GPU/RGA 指标，并保存 `vo_captures/rga_vo_*.bmp` 供复看。
`RESIZE_RGA` 单独模式使用 3840x2160 实时摄像头输入，走 `VI 3840x2160 -> RESIZE_RGA 1080x608 -> VMIX -> OSD -> VO` bind 链路，单个实时主画面宽度统一为 1080；页面动态移动裁剪框并改变 crop 尺寸，展示裁剪区域被硬件缩放放大的效果；屏幕显示 crop x/y/w/h、zoom 倍率、RESIZE_RGA 帧计数和 CPU/GPU/RGA 指标，并保存 `vo_captures/resize_rga_vo_*.bmp` 供复看。
`CSC_RGA` 单独模式使用实时摄像头输入，走 `VI -> CSC_RGA(NV12->ARGB8888) -> CSC_RGA(ARGB8888->NV12) -> VMIX -> OSD -> VO` bind 链路，页面显示颜色格式转换流程、动态通道条和两级 CSC 帧计数。
`CSC_CL` 单独模式使用 3840x2160 实时摄像头输入，走 `VI 3840x2160 -> CSC_CL(NV12->ARGB8888, 4K) -> RESIZE_RGA(1080x1920) -> OSD(ARGB8888) -> VO`；页面会明确标注 VI 输入、CSC_CL 模块 4K 输入、后置缩放和全屏显示尺寸，并显示 kernel/queue 耗时、CSC_CL/RESIZE_RGA/OSD 帧计数和 CPU/GPU/RGA 指标；运行时保存 `vo_captures/csc_cl_vo_*.bmp` 供复看。
`CAP_DEHAZE` 单独模式使用 1920x1080 有效实时摄像头输入，走 `VI 1920x1080有效画面 -> CSC_RGA BGR888 -> CAP_DEHAZE passthrough/增强 -> RESIZE_RGA 1080x608 -> VMIX -> OSD -> VO`；CAP_DEHAZE 每 1 秒在 passthrough 原图和正常去雾之间切换，屏幕显示中文数据流、guided radius、t0、beta、refine_scale=0.25、CAP_DEHAZE/RESIZE 帧计数和 CPU/GPU/RGA 指标。
`CAP_DEHAZE_OFFLINE` 单独模式不占用摄像头，使用从 `/userdata/rktohi/demo/cap_dehaze/input` 同步来的 3 张低能见度样张轮播，逐张生成原图和 CAP_DEHAZE 输出两张图做上下等尺寸对比；该页紧跟默认客户循环中的实时 `CAP_DEHAZE` 页，使用相同 guided radius、t0、beta 和 `refine_scale=0.25`，用于实时画面效果不明显时让客户看清去雾差异，并保存 `vo_captures/cap_dehaze_offline_vo_*.bmp` 供复看。
`DCP_FAST_DEHAZE` 单独模式使用实时摄像头输入，走 `VI -> CSC_RGA BGR888 -> DCP_FAST_DEHAZE passthrough/增强 -> RESIZE_RGA 1080x608 -> VMIX -> OSD -> VO`，屏幕显示中文数据流、参数、为什么这样做和实测 FPS/CPU/GPU/RGA 指标。DCP_FAST_DEHAZE 尚未纳入本轮 4K 输入通过列表。
`THERMAL` 单独模式使用 `/userdata/rktohi/demo/thermal/1.png` 和 `2.png` 原始 demo 图，一屏展示 16 种热成像伪彩模式，同一张输入图按不同色表映射，便于直接比较 RAINBOW、BLACK HOT、WHITE HOT、IRON、SEPIA 等模式差异；源图每 3 秒自动切换。页面按当前源图缓存整页 NV12 结果，避免每帧重算 16 个伪彩格，日志输出帧数、样张索引、cache 状态和 CPU/GPU/RGA 占用率，并保存 `vo_captures/thermal_vo_*.bmp` 供复看。
`THERMAL_LOWLIGHT_FUSION_CL` 单独模式不占用摄像头，使用 `scripts/run_alldemo.sh` 从 `/userdata/rktohi/build/thermal_lowlight_fusion_cl_real_preview` 同步到 `assets/loop/thermal_lowlight_fusion_cl_real_preview` 的真实预览图；每 3 秒切换 carLight、manlight、nightCar、walkingnight 样张，同屏展示 `input0_ir`、`input1_vi`、`Pyramid algo=0` 和 `TIF algo=1 radius=8` 四宫格，用于快速查看热成像微光融合模块的两种 GPU 融合算法差异；黑红热目标叠加 mode1 仍保留在模块能力中。
`CONV_CL` 单独模式使用 3840x2160 实时摄像头输入，页面侧先把 VI 的 NV12 帧下采样到处理尺寸并转换为 RGBA staging 后送入单个 CONV_CL 组，按顺序切换 kernel size 和卷积表输出 2x2 页面送 VO。默认轮播停留 30 秒：前 10 秒保留原来的 SHARPEN、EDGE、EMBOSS、BLUR 四种 3x3 卷积核同屏比较；中间 10 秒显示 RAW、SHARP 3x3、SHARP 11x11、SHARP 21x21；最后 10 秒显示 RAW、BLUR 3x3、BLUR 11x11、BLUR 21x21；屏幕显示中文数据流、阶段说明、四格帧计数、CPU/GPU/RGA 占用率和 OpenCL kernel/queue 耗时，并保存 `vo_captures/conv_cl_vo_*.bmp` 供复看。
`VMIX` 单独模式使用 3840x2160 实时摄像头输入，走 `VI 3840x2160 -> VPSS(4路480x480同源输入) -> VMIX(input0..3) -> OSD -> VO` bind 链路；VMIX 页面把四路输入做位置叠放和 alpha 透明混合，屏幕上显示中文数据流、各路 alpha、VI/VMIX 帧计数和 CPU/GPU/RGA 指标，并保存 `vo_captures/vmix_vo_*.bmp` 供复看。
`VPSS` 单独模式使用 3840x2160 实时摄像头输入，走 `VI 3840x2160 -> VPSS(4路输出) -> VMIX -> OSD -> VO` bind 链路，并在同屏展示 VPSS 多输出能力：全幅缩放、动态裁剪后缩放、水平/垂直翻转切换、中心缩放变化；页面保存 `vo_captures/vpss_vo_*.bmp` 供复看。
`CLAHE` 单独模式使用 3840x2160 实时摄像头输入，走 `VI 3840x2160 -> CLAHE 3840x2160 -> RESIZE_RGA 1080x608 -> VMIX_RGA -> OSD -> VO`；后置缩放保持 16:9 比例，CLAHE 每 1 秒在 passthrough 原图和正常增强之间切换，屏幕动态展示当前模式、clip limit、8x8 tile grid、CLAHE/RESIZE 帧计数和 CPU/GPU/RGA 指标，并保存 `vo_captures/clahe_vo_*.bmp` 供复看。
`RETINEX` 单独模式使用 3840x2160 实时摄像头输入，走 `VI 3840x2160 -> RETINEX 3840x2160 -> RESIZE_RGA 1080x608 -> VMIX_RGA -> OSD -> VO`；后置缩放保持 16:9 比例，RETINEX 每 1 秒在 passthrough 原图和正常增强之间切换，屏幕显示 gain=40、threshold、log range、RETINEX/RESIZE 帧计数和 CPU/GPU/RGA 指标，并保存 `vo_captures/retinex_vo_*.bmp` 供复看。
`RETINEX_OFFLINE` 单独模式不占用摄像头，使用 `scripts/run_alldemo.sh` 从 `/userdata/rktohi/research/retinex/normalized/exdark_boat` 同步到 `assets/loop/retinex/exdark` 的前 100 张 EXDark 低照度图片；每 1 秒切换一张，将原图缩放为 640x640 NV12 后送入 RETINEX，使用 gain=40 生成目标增强图，并做上下等尺寸对比。默认客户循环中该页紧跟实时 `RETINEX` 页，完整播放约 100 秒，并保存 `vo_captures/retinex_offline_vo_*.bmp` 供复看。
`EIS` 单独模式不占用摄像头，使用 `assets/eis/eis_shaky_640x360.h264` 抖动素材，走 `H264文件 -> VDEC -> VPSS(两路输出) -> RAW/EIS_GPU -> VMIX -> OSD -> VO`；上半屏显示原始 VPSS 分支，下半屏显示 EIS GPU 稳像分支，EIS 内部自计算帧间补偿矩阵，使用 `crop_ratio=0.08`，OSD 显示 OpenCL total/estimate/warp 耗时、fallback 状态和完整链路。该页可配合 `wbc_h264_record` 录制当前屏幕并转成 MP4，用于复看最终 GPU 效果。
`EIS_VI` 单独模式使用实时摄像头输入，走 `VI 3840x2160 -> EIS -> VPSS 1072x608 -> OSD -> VO`；该页保留 `EIS` 离线素材对比页不变，专门用于验证真实 VI 输入进入电子稳像后的显示链路。屏幕 OSD 叠加标题、真实链路、输入/输出尺寸、EIS total/estimate/warp 耗时和画面标签，日志同步输出 VI/EIS/VPSS/OSD/VO 帧计数。
`EIS_DETECT_NPU` 单独模式不占用摄像头，使用 `assets/eis/eis_shaky_640x360.h264`，走 `H264文件 -> VDEC -> VPSS(上路原图/下路EIS) -> EIS -> VPSS(显示640x360/模型640x640) -> RGA(NV12转RGB888) -> DETECT_NPU(YOLOv5) -> VMIX -> OSD -> VO`；原视频放在上方，EIS稳像结果放在下方，NPU只作为检测分支输出检测框，避免把640x640模型输入直接显示出来。屏幕叠加 EIS 耗时、VDEC/VPSS/EIS/RGA/NPU/VMIX/OSD/VO 帧计数、检测框、类别和置信度。`--only EIS_NPU` 和 `--only EIS_NPU_DETECT` 是同一页面的别名。
`TNR_CL` 单独模式不占用摄像头，使用公开视频低照度素材裁剪成 640x640 并叠加可控轻噪声后编码为 H264，运行时走 `H264文件 -> VDEC -> VPSS双路 -> 原图/TNR_CL -> VO页面`，屏幕显示输入噪声帧和降噪输出对比；当前单页展示参数为 `threshold=0.12`、`static_alpha=0.62`、`motion_alpha=0.96`，稳定区域混入更多历史帧以让演示效果更明显，运动区域仍主要使用当前帧以减少拖影。日志输出 VDEC/VPSS/TNR_CL/VO 帧计数和 motion/blend/queue GPU 耗时，用于确认 H264 解码实时展示闭环。
`WAVELET_NR_CL` 单独模式不占用摄像头，复用 `TNR_CL` 的 H264 噪声视频素材，运行时走 `H264文件 -> VDEC -> VPSS双路 -> 原图/WAVELET_NR_CL -> VO页面`，做输入/输出上下等尺寸对比，展示 Y 分量 Haar 小波空间降噪效果；当前单页展示参数为 `levels=1`、`threshold_y=6/255`、`strength=0.65`，页面明确标注这是空间降噪，不做时域融合，因此不会引入拖影。日志输出 VDEC/VPSS/WAVELET_NR_CL 处理闭环、processed 帧数、GPU kernel/queue 耗时，用于和 `TNR_CL` 的时域效果区分。
`HIGHLIGHT_SUPPRESS` 单独模式不占用摄像头，使用合成 NV12 强反光场景进入 `HIGHLIGHT_SUPPRESS`，做输入/输出上下等尺寸对比，展示 soft-knee 高光压制是否让白色刺眼反光变柔和。
`HIGHLIGHT_SUPPRESS_VI` 单独模式使用 3840x2160 实时摄像头输入，走 `VI 3840x2160 -> HIGHLIGHT_SUPPRESS -> RESIZE_RGA 1080x608 -> VMIX_RGA -> OSD -> VO`；页面每 1 秒在 `BYPASS` 原图和正常高光压制输出之间自动切换，屏幕显示 low/high/knee/ratio/strength 参数、VI/HIGHLIGHT_SUPPRESS/RESIZE 帧计数、GPU/CPU模块耗时和真实 bind 链路。`--only VI_HIGHLIGHT_SUPPRESS` 是同一页面的别名。
`EDOF_CL` 单独模式使用 `assets/loop/edof/mfi_whu` 的 `a.jpg/b.jpg/fused.png` 样张做三栏对比，每 3 秒切换一组；默认客户页使用参考融合结果稳定展示并避免慢速 OpenCL 线程影响 Ctrl+C 清理，页面按样张索引缓存整页 NV12 结果，日志输出帧数、样张索引、更新次数、模式、cache 状态和 CPU/GPU/RGA 指标，并保存 `vo_captures/edof_cl_vo_*.bmp` 供复看。需要验证实时模块路径时可显式设置 `ALLDEMO_EDOF_CL_LIVE=1`。
`MCF_FUSION_CL` 单独模式使用 `assets/loop/mcf_fusion` 的彩色图、单色细节图和参考融合图做对比，每 3 秒切换一组；程序优先调用 `MEDIA_MCF_FUSION_CL` 的 OpenCL 模块生成输出，如果当前 GPU/OpenCL 路径初始化失败则保留参考融合结果上屏，避免展示页空白。页面按样张索引缓存整页 NV12 结果，日志输出帧数、样张索引、更新次数、模式、cache 状态、CPU/GPU/RGA 和 CL total/stats/fusion 耗时，并保存 `vo_captures/mcf_fusion_cl_vo_*.bmp` 供复看。
`DUALVIEW` 单独模式参考 `/userdata/rktohi/demo/dualview` 示例生成两路 RGB888 输入：input0 纯红、input1 纯蓝，主画面同时显示 input0、input1、side-by-side 输出和 line-by-line 输出，不占用摄像头；屏幕上显示中文数据流和实测 FPS/CPU/GPU/RGA 指标。
`STEREO_3D` 单独模式使用实时摄像头输入，走 `VI -> VPSS(2路同源NV12：一路原图、一路VPSS旋转90度) -> STEREO_3D(SIDE_BY_SIDE合并) -> VMIX -> OSD -> VO` bind 链路；不再做蓝/红颜色 tint，屏幕显示一个合并输出，并显示帧计数和 CPU/GPU/RGA 占用率。STEREO_3D 尚未纳入本轮 4K 输入通过列表，需先修复 STEREO_3D 输出帧不增长问题。
`PANO` 单独模式使用 `assets/loop/pano/sample2` 的六张图片和 PTO 标定文件，主画面显示六路输入图和 panorama 输出，不占用摄像头；默认客户页走真实 PANO 模块路径，按 PTO 输出完整全景域 8378x4190 NV12，再由页面缩小到显示框，并按 JPEG EXIF orientation 修正输入方向。需要临时回到 reference-strip 兜底预览时可显式设置 `ALLDEMO_PANO_LIVE=0`。页面按输出生成状态缓存整页 NV12 结果，日志输出帧数、输出状态、模式、cache 状态、预览尺寸、完整全景域和 CPU/GPU/RGA 指标，并保存 `vo_captures/pano_vo_*.bmp` 供复看。
`DETECT_NPU` 单独模式是产品化检测模块展示页，底层复用 `MEDIA_NPU/RKNN` 通用运行时，链路是 `H264文件 -> VDEC -> RGA(NV12转RGB) -> DETECT_NPU(YOLOv5) -> RGA(RGB转NV12) -> OSD -> VO`，不占用摄像头；屏幕叠加检测框、类别、置信度和 VDEC/RGA/NPU/OSD/VO 帧计数。`--only NPU` 和 `--only YOLO_DETECT_NPU` 保留为兼容别名。
`FRUIT_DETECT_NPU` 单独模式不新增底层模块，复用现有 `DETECT_NPU`/YOLOv5/RKNN，使用网上公开单类水果图片生成的 640x640 H264 轮播素材，每段只展示一种水果：apple、banana、orange；链路为 `H264水果图片轮播 -> VDEC -> RGA(NV12转RGB) -> DETECT_NPU -> RGA(RGB转NV12) -> OSD -> VO`，页面只过滤展示这三类水果检测框和置信度。`--only FRUIT_NPU` 和 `--only FRUIT_DETECT` 是同一页面别名。
`SEGMENT_NPU` 单独模式是产品化语义分割展示页，使用 RKNN Toolkit 2.3.2 转换的 `PP-LiteSeg Cityscapes` RK3588 INT8 模型，输入为 `assets/loop/npu/bus_640x640.h264` 真实 H264 视频流，链路为 `H264 -> VDEC -> RGA(512x512 RGB) -> SEGMENT_NPU旁路抽帧 -> CPU mask overlay -> 页面VO`；页面上半屏显示 VDEC 视频帧，下半屏显示 CPU 生成的像素级 mask overlay，OSD 显示 RKNN run/post/total 耗时、视频帧号和 mask 帧号。该页主视频正常逐帧刷新，分割旁路每 4 帧投递一次 NPU 请求；NPU 忙时沿用上一张 mask，用于展示 NPU 像素级视觉能力，和 `DETECT_NPU` 的检测框能力互补。

## 快速自检

演示前建议先跑完整现场健康检查，不占用屏幕/摄像头：

```bash
/userdata/alldemo/scripts/run_alldemo.sh --self-test
```

`--self-test` 会检查 license、摄像头节点、DRM、media 库、RTSP 端口、NPU 模型、AVM/PANO/SVM3D 素材和循环图片解码。

只检查循环展示素材是否能被当前二进制解码：

```bash
/userdata/alldemo/scripts/run_alldemo.sh --asset-check
```

正常运行时，屏幕标题区会显示当前页码、实测 FPS、单帧耗时、CPU/GPU/RGA 占用；底部状态栏会重复显示同一组实时指标，方便展示时直接说明“实时性”和“低 CPU 占用”。摄像头未出帧时主画面会明确显示 `CAMERA OFFLINE`。如果当前系统没有暴露可读 GPU/RGA load 节点，对应指标会显示 `N/A`。
当前 `--only VI` 使用 3840x2160 实时摄像头输入，后续保持原页面显示尺寸，走 `VI 3840x2160 -> RESIZE_RGA 1080x608 -> VMIX(NV12) -> OSD -> VO` bind 显示链路，不再 CPU 拷贝 VI 图像帧；屏幕下方会显示数据流 bind 面板，直接列出谁绑定到谁，例如 `VI0.output -> RESIZE_RGA61.input0`、`RESIZE_RGA61.output0 -> VMIX80.input0`、`VMIX80.output0 -> OSD81.input`、`OSD81.output0 -> VO0.input0`。端口名由程序在 `output0/output` 和 `input0/input` 中自动探测，绑定成功后按实际命中的端口显示；当前 4K 路径用实时帧计数和 VO 抓帧验收。
实现方式在 `src/alldemo.c`：

- `bind_first_match()` 负责尝试端口组合并调用 `MEDIA_SYS_Bind()`。
- `bind_vi_vmix_osd_vo()` 负责建立 `VI -> VMIX -> OSD -> VO` 三段 bind，并保存实际命中的端口名。
- `update_vi_bind_flow_overlay()` 负责把这条稳定输入数据流画到 `DISPLAY_OSD_GRP` 的 OSD region 上。

`--only VPSS` 已接入 `VI -> VPSS -> VMIX -> OSD -> VO` bind 显示链路，屏幕会显示“VI 输入经过 VPSS 处理后上屏”的中文数据流说明。VPSS 四路输出分别缩放/处理为 480x480 后绑定到 VMIX 四个输入，其中裁剪窗口、翻转状态和缩放裁剪会动态变化；VMIX 只负责四宫格摆放，不依赖 VMIX 缩放能力。

小窗体右上角状态含义：

- `LIVE`：真实模块链路正在输出，例如当前 OSD tile 使用 `camera frame -> OSD module -> tile`，RESIZE_RGA tile 使用 `camera frame -> RESIZE_RGA module -> tile`，VPSS tile 使用 `camera frame -> VPSS module -> tile`
- `PROBED`：模块初始化探测通过，但默认展示未接实时输出
- `LOOP`：使用循环素材展示
- `SYNTH`：合成占位展示，不代表真实算法进度
- `OFFLINE`：资源缺失或模块未启用

如果屏幕出现条纹或闪烁，先跑纯色显示链路测试：

```bash
/userdata/alldemo/scripts/run_alldemo.sh --solid-test
```

`--solid-test` 不读取摄像头、不加载素材、不做动画；如果它也异常，优先检查 VO/屏参/线缆/屏幕链路。

## 模块输入策略

具体效果和输入来源见 `assets/effect_manifest.json`。

默认运行优先保证现场稳定上屏：实时摄像头进入主画面，默认轮播只包含已接通展示闭环的页面；重算法真实初始化和未进入默认轮播的模块用 `--only <tile>` 逐个展示。更重的初始化探测可以单独执行：

```bash
/userdata/alldemo/scripts/run_alldemo.sh --probe
```

`--probe` 会尝试创建 OpenCL/Vulkan/RGA/NPU/全景/AVM/SVM3D 等模块，用于联调，不建议现场主展示默认打开。
