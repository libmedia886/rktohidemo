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

默认参数适配当前设备：

- 屏幕：DSI/MIPI 竖屏 `1080x1920`
- 摄像头：`/dev/video-camera0`
- 主画面：默认每个功能模块单独一屏轮播，屏幕上显示当前模块名、页码、CPU 占用和 GPU 占用
- 缺真实传感器输入的模块：用生成帧/循环素材方式参与

按 `Ctrl+C` 退出。

如果只想固定在第一个功能模块屏：

```bash
/userdata/alldemo/scripts/run_alldemo.sh --no-rotate-main
```

默认分页策略：`VI` 到 `PIC_IO` 每个功能模块单独一屏；`RTSP_SEND`、`RTSP_RECV` 不参与默认功能屏；`LICENSE` 只作为健康状态显示，不占功能页。后续确认每个模块的真实效果后，再考虑把低负载页面合并。

如果要逐个验证某个小窗体，不让其它实时算法一起运行：

```bash
/userdata/alldemo/scripts/run_alldemo.sh --only VPSS
/userdata/alldemo/scripts/run_alldemo.sh --only OSD
/userdata/alldemo/scripts/run_alldemo.sh --only RESIZE_RGA
/userdata/alldemo/scripts/run_alldemo.sh --only CSC_RGA
/userdata/alldemo/scripts/run_alldemo.sh --only CSC_CL
/userdata/alldemo/scripts/run_alldemo.sh --only TRANSFORM
/userdata/alldemo/scripts/run_alldemo.sh --only CAP_DEHAZE
/userdata/alldemo/scripts/run_alldemo.sh --only DCP_FAST_DEHAZE
/userdata/alldemo/scripts/run_alldemo.sh --only THERMAL
/userdata/alldemo/scripts/run_alldemo.sh --only CONV_CL
/userdata/alldemo/scripts/run_alldemo.sh --only CLAHE
/userdata/alldemo/scripts/run_alldemo.sh --only RETINEX
/userdata/alldemo/scripts/run_alldemo.sh --only EDOF_CL
/userdata/alldemo/scripts/run_alldemo.sh --only DUALVIEW
/userdata/alldemo/scripts/run_alldemo.sh --only STEREO_3D
/userdata/alldemo/scripts/run_alldemo.sh --only PANO
```

`--only <tile>` 会把主画面固定到指定小窗体，并且只初始化该小窗体需要的实时模块；未接入真实实时链路的 tile 会显示循环素材或合成占位，且不会强制打开摄像头。
`TRANSFORM` 单独模式使用合成 NV12 输入和本地 LUT，不占用摄像头。
`VO` 单独模式不占用摄像头，页面直接展示 MIPI/DSI 输出、1080x1920、NV12 plane、动态扫描条和彩条。
`VO` 单独模式会动态调用 `MEDIA_VO_FreezeMain`、`MEDIA_VO_FreezePlane` 和 `MEDIA_VO_HidePlane`，展示正常刷新、主显示冻结、plane 冻结、短暂隐藏再恢复。隐藏只持续约 1 秒，退出时会强制恢复显示。
`OSD` 单独模式使用实时摄像头输入，走 `VI -> OSD -> VMIX -> OSD -> VO` bind 链路，页面展示动态 region 坐标、zorder 层级、alpha 透明度、enabled 告警开关和两级 OSD 帧计数。
`RGA` 单独模式使用实时摄像头输入，走 `VI -> RGA -> VMIX -> OSD -> VO` bind 链路，页面会动态轮播 COPY、移动 CROP+SCALE、水平/垂直翻转和 90/180/270 度旋转，并显示 RGA 数据流和当前硬件操作。
`RESIZE_RGA` 单独模式使用实时摄像头输入，走 `VI -> RESIZE_RGA -> VMIX -> OSD -> VO` bind 链路，页面动态移动裁剪框并改变 crop 尺寸，展示裁剪区域被硬件缩放放大的效果。
`CSC_RGA` 单独模式使用实时摄像头输入，走 `VI -> CSC_RGA(NV12->ARGB8888) -> CSC_RGA(ARGB8888->NV12) -> VMIX -> OSD -> VO` bind 链路，页面显示颜色格式转换流程、动态通道条和两级 CSC 帧计数。
`CSC_CL` 单独模式使用实时摄像头输入，走 `VI -> CSC_CL(NV12->ARGB8888) -> CSC_CL(ARGB8888->NV12) -> VMIX -> OSD -> VO` bind 链路，页面用大号数据流展示 OpenCL/GPU 颜色矩阵转换，并显示 kernel/queue 耗时和两级 CSC_CL 帧计数。
`CAP_DEHAZE` 单独模式使用实时摄像头输入，程序把 VI 的 NV12 帧转换为 RGB 后送入 CAP_DEHAZE，主画面左右对比原始 VI 输入和去雾输出。
`DCP_FAST_DEHAZE` 单独模式使用实时摄像头输入，程序把 VI 的 NV12 帧转换为 RGB 后送入 DCP_FAST_DEHAZE，主画面左右对比原始 VI 输入和暗通道先验去雾输出。
`THERMAL` 单独模式使用 `/userdata/rktohi/demo/thermal/1.png` 和 `2.png` 原始 demo 图，一屏展示 16 种热成像伪彩模式，同一张输入图按不同色表映射，便于直接比较 RAINBOW、BLACK HOT、WHITE HOT、IRON、SEPIA 等模式差异；源图每 3 秒自动切换。
`CONV_CL` 单独模式使用合成 RGBA 输入，不占用摄像头。
`VPSS` 单独模式使用实时摄像头输入，并在同屏展示 VPSS 多输出能力：全幅缩放、动态裁剪后缩放、水平/垂直翻转切换、中心缩放变化。
`CLAHE` 单独模式使用实时摄像头输入，走 `VI -> VPSS` 分成两路：`VPSS(output0) -> VMIX(input0)` 显示原始输入，`VPSS(output1) -> CLAHE -> VMIX(input1) -> OSD -> VO` 显示增强输出，并动态展示 clip limit 和帧计数。
`RETINEX` 单独模式使用实时摄像头输入，走 `VI -> VPSS` 分成两路：`VPSS(output0) -> VMIX(input0)` 显示原始输入，`VPSS(output1) -> RETINEX -> VMIX(input1) -> OSD -> VO` 显示光照校正输出，并展示 gain、threshold、帧计数和 bind 链路。
`EDOF_CL` 单独模式使用 `assets/loop/edof/mfi_whu` 的 `a.jpg/b.jpg/fused.png` 样张做三栏对比，每 3 秒切换一组。
`DUALVIEW` 单独模式参考 `/userdata/rktohi/demo/dualview` 示例生成两路 RGB888 输入：input0 纯红、input1 纯蓝，主画面同时显示 input0、input1、side-by-side 输出和 line-by-line 输出，不占用摄像头。
`STEREO_3D` 单独模式使用摄像头输入。
`PANO` 单独模式使用 `assets/loop/pano/sample2` 的六张图片和 PTO 标定文件，主画面显示六路输入图和 panorama 输出，不占用摄像头。

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

正常运行时，屏幕标题区会显示当前页码、CPU 占用和 GPU 占用；底部状态栏会显示摄像头出帧数、素材加载数、模块激活数、license/DRM/NPU 状态。摄像头未出帧时主画面会明确显示 `CAMERA OFFLINE`。如果当前系统没有暴露可读 GPU load 节点，GPU 会显示 `N/A`。
当前 `--only VI` 已接入 `VI -> VMIX(NV12) -> OSD -> VO` bind 显示链路，不再 CPU 拷贝 VI 图像帧；屏幕下方会显示数据流 bind 面板，直接列出谁绑定到谁，例如 `VI0.output -> VMIX80.input0`、`VMIX80.output0 -> OSD81.input`、`OSD81.output0 -> VO0.input0`。端口名由程序在 `output0/output` 和 `input0/input` 中自动探测，绑定成功后按实际命中的端口显示。
实现方式在 `src/alldemo.c`：

- `bind_first_match()` 负责尝试端口组合并调用 `MEDIA_SYS_Bind()`。
- `bind_vi_vmix_osd_vo()` 负责建立 `VI -> VMIX -> OSD -> VO` 三段 bind，并保存实际命中的端口名。
- `update_vi_bind_flow_overlay()` 负责把这三段数据流画到 `DISPLAY_OSD_GRP` 的 OSD region 上。

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

默认运行优先保证现场稳定上屏：实时摄像头进入主画面，其它缺少真实传感器的模块以循环图片/生成帧形式展示在 tile 中。重算法真实初始化探测可以单独执行：

```bash
/userdata/alldemo/scripts/run_alldemo.sh --probe
```

`--probe` 会尝试创建 OpenCL/Vulkan/RGA/NPU/全景/AVM/SVM3D 等模块，用于联调，不建议现场主展示默认打开。
