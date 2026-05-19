# DEFECT-CUSTOMER-DEMO-20260519-003: RGA 页面过早切换

状态: Fixed
分类: Software Defect
页面: RGA

## 问题描述

`RGA VI RGA VMIX` 页面还没有播放完整效果就切到下一页，客户看不完整 RGA 动态变化。

## 大概解决方案

检查默认轮播停留时间、RGA 页面内部阶段时长和完成条件。把 RGA 动效拆成固定阶段并保证至少完整展示一轮，再允许默认轮播切页。

## 整改记录

默认轮播对 RGA 页面使用按页停留时间，停留时间不小于 `RGA_OP_SECONDS * RGA_DEMO_OP_COUNT`，保证 COPY、裁剪放大、水平翻转、垂直翻转、90/180/270 度旋转至少完整展示一轮。

## 验证

`timeout -s INT -k 5s 25s ./scripts/run_alldemo.sh --only RGA` 运行中观察到 7 个 RGA 操作全部出现，并生成 `vo_captures/rga_vo_000150.bmp` 到 `vo_captures/rga_vo_000600.bmp`。
