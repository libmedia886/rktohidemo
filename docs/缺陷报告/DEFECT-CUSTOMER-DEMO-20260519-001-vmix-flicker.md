# DEFECT-CUSTOMER-DEMO-20260519-001: VMIX 4 INPUT 页面闪烁

状态: Fixed
分类: Software Defect
页面: VMIX

## 问题描述

`VMIX 4 INPUT` 页面现场观察到闪烁，影响客户对四路合成稳定性的判断。

## 大概解决方案

复现并抓取短视频或 VO/WBC 录制片段；检查 VMIX 输入帧更新、OSD 叠加刷新、VO 提交节奏和 buffer 复用。优先保证四路输入、OSD 和 VO 使用稳定的帧节奏，必要时降低页面动态变化频率。

## 整改记录

VMIX 四路合成改为固定 2x2 不重叠布局，取消半透明叠放；静态说明层只初始化刷新，动态帧计数单独刷新。

## 验证

`timeout -s INT -k 5s 12s ./scripts/run_alldemo.sh --only VMIX` 运行成功，生成 `vo_captures/vmix_vo_000150.bmp`，退出后无 alldemo 残留进程。
