# DEFECT-CUSTOMER-DEMO-20260519-010 RGA/RESIZE_RGA 单实时主画面分辨率不统一

状态：Fixed
分类：Product Defect
页面：RGA / RESIZE_RGA

## 问题

`--only RGA` 和 `--only RESIZE_RGA` 仍按 `640x640` 工作/显示帧接入 VMIX，和当前单个实时主画面宽度应为 1080 的规则不一致。

旧链路：

- `RGA`: `VI 3840x2160 -> RGA 640x640 -> VMIX -> OSD -> VO`
- `RESIZE_RGA`: `VI 3840x2160 -> RESIZE_RGA 640x640 -> VMIX -> OSD -> VO`

## 修复

RGA 与 RESIZE_RGA 的 bind 单页均改为 `1080x608` 显示帧，stride 为 `1088`，并使用 `DISPLAY_VMIX_LAYOUT_VI_BIG` 接入 VMIX。

新链路：

- `RGA`: `VI 3840x2160 -> RGA 1080x608 -> VMIX -> OSD -> VO`
- `RESIZE_RGA`: `VI 3840x2160 -> RESIZE_RGA 1080x608 -> VMIX -> OSD -> VO`

屏幕文案和 README/manifest/验收记录同步改为 1080 宽主实时画面。

## 验收要求

- runtime probe 必须确认下游 buffer 流转，而不仅是 VI 有帧。
- RGA 验收看 `VI -> RGA -> VMIX -> OSD -> VO` bind 兼容性和 `vi_frames/rga_frames/display_osd_frames` 增长。
- RESIZE_RGA 验收看 `VI -> RESIZE_RGA -> VMIX -> OSD -> VO` bind 兼容性和 `vi_frames/resize_frames/display_osd_frames` 增长。

## 验收记录

`timeout -s INT -k 5s 7s ./scripts/run_alldemo.sh --only RGA` 运行到 SIGINT 退出，bind 兼容性显示 `RGA_59.output0 (1080x608, stride=1088) -> VMIX_80.input0`，`vi_frames/rga_frames/display_osd_frames` 增长到 `183/183/183`，并保存 `/userdata/alldemo/vo_captures/rga_vo_000150.bmp`。

`timeout -s INT -k 5s 7s ./scripts/run_alldemo.sh --only RESIZE_RGA` 运行到 SIGINT 退出，bind 兼容性显示 `RESIZE_RGA_61.output0 (1080x608, stride=1088) -> VMIX_80.input0`，动态重建段内 `vi_frames/resize_frames/display_osd_frames` 增长到 `150/31/149`，并保存 `/userdata/alldemo/vo_captures/resize_rga_vo_000150.bmp`。退出时 `DISPLAY_VMIX_INPUT_POOL id=6` 已正常销毁，未再出现 buffer 未归还。
