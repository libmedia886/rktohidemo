# RKTohi AllDemo

独立总控 demo 工程，只拷贝 `media_api.h`、`libmedia.so` 和必要素材，不拷贝 `rktohi/src` 或原 demo 源码。

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

默认客户演示分页策略：轮播用户容易看懂、画面效果明显、稳定上屏的页面：`VI`、`VPSS`、`VMIX`、`OSD`、`RGA`、`RESIZE_RGA`、`CSC_CL`、`CLAHE`、`RETINEX`、`CAP_DEHAZE`、`CONV_CL`、`TRANSFORM`、`THERMAL`、`EDOF_CL`、`MCF_FUSION_CL`、`PANO`。偏工程验证或调试意味更强的 `VO`、`WBC`、`CSC_RGA`、`STEREO_3D` 保留在工程演示或 `--only` 模式，避免客户现场误解为黑屏、冻结、格式细节或调试页。

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
/userdata/alldemo/scripts/run_alldemo.sh --only DCP_FAST_DEHAZE
/userdata/alldemo/scripts/run_alldemo.sh --only THERMAL
/userdata/alldemo/scripts/run_alldemo.sh --only CONV_CL
/userdata/alldemo/scripts/run_alldemo.sh --only VMIX
/userdata/alldemo/scripts/run_alldemo.sh --only CLAHE
/userdata/alldemo/scripts/run_alldemo.sh --only RETINEX
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
`CAP_DEHAZE` 单独模式使用 3840x2160 实时摄像头输入，走 `VI 3840x2160 -> CSC_RGA RGB888 4K -> CAP_DEHAZE 4K passthrough/增强 -> RESIZE_RGA 1080x608 -> VMIX -> OSD -> VO`；CAP_DEHAZE 每 1 秒在 passthrough 原图和正常去雾之间切换，屏幕显示中文数据流、guided radius、t0、beta 参数、CAP_DEHAZE/RESIZE 帧计数和 CPU/GPU/RGA 指标。
`DCP_FAST_DEHAZE` 单独模式使用实时摄像头输入，走 `VI -> CSC_RGA RGB888 -> DCP_FAST_DEHAZE passthrough/增强 -> RESIZE_RGA 1080x608 -> VMIX -> OSD -> VO`，屏幕显示中文数据流、参数、为什么这样做和实测 FPS/CPU/GPU/RGA 指标。DCP_FAST_DEHAZE 尚未纳入本轮 4K 输入通过列表。
`THERMAL` 单独模式使用 `/userdata/rktohi/demo/thermal/1.png` 和 `2.png` 原始 demo 图，一屏展示 16 种热成像伪彩模式，同一张输入图按不同色表映射，便于直接比较 RAINBOW、BLACK HOT、WHITE HOT、IRON、SEPIA 等模式差异；源图每 3 秒自动切换。页面按当前源图缓存整页 NV12 结果，避免每帧重算 16 个伪彩格，日志输出帧数、样张索引、cache 状态和 CPU/GPU/RGA 占用率，并保存 `vo_captures/thermal_vo_*.bmp` 供复看。
`CONV_CL` 单独模式使用 3840x2160 实时摄像头输入，页面侧先把 VI 的 NV12 帧下采样到处理尺寸并转换为 RGBA staging 后送入单个 CONV_CL 组，按顺序加载 SHARPEN、EDGE、EMBOSS、BLUR 四个 3x3 卷积核，再把四个真实 OpenCL 输出合成为 2x2 页面送 VO；屏幕显示中文数据流、四个卷积核帧计数、CPU/GPU/RGA 占用率和 OpenCL kernel/queue 耗时，并保存 `vo_captures/conv_cl_vo_*.bmp` 供复看。
`VMIX` 单独模式使用 3840x2160 实时摄像头输入，走 `VI 3840x2160 -> VPSS(4路480x480同源输入) -> VMIX(input0..3) -> OSD -> VO` bind 链路；VMIX 页面把四路输入做位置叠放和 alpha 透明混合，屏幕上显示中文数据流、各路 alpha、VI/VMIX 帧计数和 CPU/GPU/RGA 指标，并保存 `vo_captures/vmix_vo_*.bmp` 供复看。
`VPSS` 单独模式使用 3840x2160 实时摄像头输入，走 `VI 3840x2160 -> VPSS(4路输出) -> VMIX -> OSD -> VO` bind 链路，并在同屏展示 VPSS 多输出能力：全幅缩放、动态裁剪后缩放、水平/垂直翻转切换、中心缩放变化；页面保存 `vo_captures/vpss_vo_*.bmp` 供复看。
`CLAHE` 单独模式使用 3840x2160 实时摄像头输入，走 `VI 3840x2160 -> CLAHE 3840x2160 -> RESIZE_RGA 1080x608 -> VMIX_RGA -> OSD -> VO`；后置缩放保持 16:9 比例，CLAHE 每 1 秒在 passthrough 原图和正常增强之间切换，屏幕动态展示当前模式、clip limit、8x8 tile grid、CLAHE/RESIZE 帧计数和 CPU/GPU/RGA 指标，并保存 `vo_captures/clahe_vo_*.bmp` 供复看。
`RETINEX` 单独模式使用 3840x2160 实时摄像头输入，走 `VI 3840x2160 -> RETINEX 3840x2160 -> RESIZE_RGA 1080x608 -> VMIX_RGA -> OSD -> VO`；后置缩放保持 16:9 比例，RETINEX 每 1 秒在 passthrough 原图和正常增强之间切换，屏幕显示 gain、threshold、log range、RETINEX/RESIZE 帧计数和 CPU/GPU/RGA 指标，并保存 `vo_captures/retinex_vo_*.bmp` 供复看。
`EDOF_CL` 单独模式使用 `assets/loop/edof/mfi_whu` 的 `a.jpg/b.jpg/fused.png` 样张做三栏对比，每 3 秒切换一组；默认客户页使用参考融合结果稳定展示并避免慢速 OpenCL 线程影响 Ctrl+C 清理，页面按样张索引缓存整页 NV12 结果，日志输出帧数、样张索引、更新次数、模式、cache 状态和 CPU/GPU/RGA 指标，并保存 `vo_captures/edof_cl_vo_*.bmp` 供复看。需要验证实时模块路径时可显式设置 `ALLDEMO_EDOF_CL_LIVE=1`。
`MCF_FUSION_CL` 单独模式使用 `assets/loop/mcf_fusion` 的彩色图、单色细节图和参考融合图做对比，每 3 秒切换一组；程序优先调用 `MEDIA_MCF_FUSION_CL` 的 OpenCL 模块生成输出，如果当前 GPU/OpenCL 路径初始化失败则保留参考融合结果上屏，避免展示页空白。页面按样张索引缓存整页 NV12 结果，日志输出帧数、样张索引、更新次数、模式、cache 状态、CPU/GPU/RGA 和 CL total/stats/fusion 耗时，并保存 `vo_captures/mcf_fusion_cl_vo_*.bmp` 供复看。
`DUALVIEW` 单独模式参考 `/userdata/rktohi/demo/dualview` 示例生成两路 RGB888 输入：input0 纯红、input1 纯蓝，主画面同时显示 input0、input1、side-by-side 输出和 line-by-line 输出，不占用摄像头；屏幕上显示中文数据流和实测 FPS/CPU/GPU/RGA 指标。
`STEREO_3D` 单独模式使用实时摄像头输入，走 `VI -> VPSS(2路同源NV12：一路原图、一路VPSS旋转90度) -> STEREO_3D(SIDE_BY_SIDE合并) -> VMIX -> OSD -> VO` bind 链路；不再做蓝/红颜色 tint，屏幕显示一个合并输出，并显示帧计数和 CPU/GPU/RGA 占用率。STEREO_3D 尚未纳入本轮 4K 输入通过列表，需先修复 STEREO_3D 输出帧不增长问题。
`PANO` 单独模式使用 `assets/loop/pano/sample2` 的六张图片和 PTO 标定文件，主画面显示六路输入图和 panorama 输出，不占用摄像头；默认客户页走真实 PANO 模块路径，按 PTO 输出完整全景域 8378x4190 NV12，再由页面缩小到显示框，并按 JPEG EXIF orientation 修正输入方向。需要临时回到 reference-strip 兜底预览时可显式设置 `ALLDEMO_PANO_LIVE=0`。页面按输出生成状态缓存整页 NV12 结果，日志输出帧数、输出状态、模式、cache 状态、预览尺寸、完整全景域和 CPU/GPU/RGA 指标，并保存 `vo_captures/pano_vo_*.bmp` 供复看。
`NPU` 单独模式保留为联调入口，链路目标是 `H264文件 -> VDEC -> RGA(NV12转RGB) -> NPU(YOLOv5) -> RGA(RGB转NV12) -> VMIX -> OSD -> VO`，不占用摄像头。当前设备上该 H264 样例会触发 VDEC `info_change`，尚未纳入第一版展示闭环；演示前不要把它放进默认轮播。

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
