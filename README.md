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
- 主画面：默认每 5 秒轮播一个下方小窗体内容 + 竖屏视觉墙
- 缺真实传感器输入的模块：用生成帧/循环素材方式参与

按 `Ctrl+C` 退出。

如果只想固定主画面为实时摄像头：

```bash
/userdata/alldemo/scripts/run_alldemo.sh --no-rotate-main
```

如果要逐个验证某个小窗体，不让其它实时算法一起运行：

```bash
/userdata/alldemo/scripts/run_alldemo.sh --only VPSS
/userdata/alldemo/scripts/run_alldemo.sh --only OSD
/userdata/alldemo/scripts/run_alldemo.sh --only RESIZE_RGA
/userdata/alldemo/scripts/run_alldemo.sh --only CSC_RGA
/userdata/alldemo/scripts/run_alldemo.sh --only TRANSFORM
```

`--only <tile>` 会把主画面固定到指定小窗体，并且只初始化该小窗体需要的实时模块；未接入真实实时链路的 tile 会显示循环素材或合成占位，且不会强制打开摄像头。
`TRANSFORM` 单独模式使用合成 NV12 输入和本地 LUT，不占用摄像头。

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

正常运行时，底部状态栏会显示摄像头出帧数、素材加载数、模块激活数、license/DRM/RTSP/NPU 状态。摄像头未出帧时主画面会明确显示 `CAMERA OFFLINE`。

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
