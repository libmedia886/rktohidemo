# DEFECT-CUSTOMER-DEMO-20260519-011 CAP_DEHAZE 应先算法后缩放

状态：Partially Fixed
分类：Product Defect / Performance Risk
页面：CAP_DEHAZE

## 问题

CAP_DEHAZE 之前是页面侧从 VI 4K 帧下采样到 640x640/RGB 后再送算法模块，展示方式和 CLAHE/RETINEX 当前规则不一致。

期望规则：

`VI 3840x2160 -> 算法处理 4K -> RESIZE_RGA 1080x608 -> VMIX/OSD/VO`

## 当前修复

CAP_DEHAZE 已改为 bind 链路：

`VI 3840x2160 -> CSC_RGA RGB888 4K -> CAP_DEHAZE 4K passthrough/增强 -> RESIZE_RGA 1080x608 -> VMIX -> OSD -> VO`

CAP_DEHAZE 每 1 秒切换 passthrough 和增强输出，显示端保持 1080x608。

## Runtime Probe

命令：

`timeout -s INT -k 5s 6s ./scripts/run_alldemo.sh --only CAP_DEHAZE`

已确认：

- `VI_0.output (3840x2160, stride=3840) -> CSC_RGA_64.input`
- `CSC_RGA_64.output0 (3840x2160, stride=11520, RGB888) -> CAP_DEHAZE_66.input`
- `CAP_DEHAZE_66.output0 (3840x2160, stride=11520, RGB888) -> RESIZE_RGA_116.input0`
- `RESIZE_RGA_116.output0 (1080x608, stride=1088) -> VMIX_80.input0`
- `vi/csc/dehaze/resize/display_osd` 增长到 `154/70/70/70/70`
- 退出时 `WORK_POOL_RGB/WORK_POOL_OUT/DISPLAY_VMIX_INPUT_POOL` 均正常销毁

## 剩余风险

4K RGB DEHAZE 增强阶段慢于 VI 上游输入，运行中仍会出现 `CSC_RGA: 申请输出 buffer 失败`，表示上游输出池满并发生丢帧。当前修复满足“buffer 有流转、算法先处理、最后缩放、退出清理干净”，但流畅性和丢帧仍需继续优化。
