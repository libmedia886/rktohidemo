# DEFECT-CUSTOMER-DEMO-20260519-009: OSD 单个实时主画面未按 1080 宽显示

状态: Fixed
分类: Product Defect
页面: OSD

## 问题描述

用户复看指出 `OSD VI OSD` 页面不符合单个实时主画面统一规则。当前页面仍记录并实现为：

`VI 3840x2160 -> RESIZE_RGA 640x640 -> OSD -> VMIX -> OSD -> VO`

这会把单个实时主画面维持在 640x640 工作/显示逻辑，和已确认的规则不一致。

## 期望行为

- 单个实时主画面宽度统一为屏幕宽 `1080`。
- 4K 16:9 实时输入应等比例显示为 `1080x608`，stride `1088`。
- 如果 OSD 算法/模块内部仍必须在 640x640 工作帧上处理，屏幕数据流必须明确区分“模块工作帧”和“最终主画面显示尺寸”。
- 优先评估是否可改为：`VI 3840x2160 -> OSD/处理 -> RESIZE_RGA 1080x608 -> VMIX/页面OSD -> VO`，或在保留工作帧时追加后置 1080 宽显示。

## 已确认现状

- `README.md` 中 OSD 单页仍写为 `RESIZE_RGA 640x640 -> OSD`。
- `src/alldemo.c` 的 OSD 页面说明仍显示 `OSD模块工作帧为640x640`。
- `module_flow_note("OSD")` 仍是 `VI 640x640 NV12 -> OSD动态图层 -> VMIX -> VO`。

## 修复记录

2026-05-19 已改为：

`VI 3840x2160 -> RESIZE_RGA 1080x608 -> OSD -> VMIX -> 页面OSD -> VO`

live OSD 输入/输出、VMIX 输入窗口和页面文案均改为 1080x608，stride 1088。

## 验收要求

- `--only OSD` 屏幕数据流明确显示 4K 输入、OSD 工作位置、最终显示宽度。
- 运行日志能证明下游 buffer 流转，不只看 VI 帧计数。
- 验收日志需看到 RESIZE_RGA/OSD/页面 OSD 帧计数随 VI 增长。

## 验证

`timeout -s INT -k 5s 8s ./scripts/run_alldemo.sh --only OSD` 运行成功。端口兼容检查确认：

- `VI_0.output (3840x2160, stride=3840) -> RESIZE_RGA_61.input0`
- `RESIZE_RGA_61.output0 (1080x608, stride=1088) -> OSD_60.input`
- `OSD_60.output0 (1080x608, stride=1088) -> VMIX_80.input0`
- `VMIX_80.output0 (1080x1920, stride=1088) -> OSD_81.input -> VO_0.input0`

buffer 流转确认：`vi_frames=216 resize_frames=216 osd_frames=215 display_osd_frames=214`。退出时 VI 停止、模块解绑、pool 销毁完成。
