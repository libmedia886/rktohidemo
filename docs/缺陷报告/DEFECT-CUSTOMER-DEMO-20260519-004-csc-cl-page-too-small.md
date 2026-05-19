# DEFECT-CUSTOMER-DEMO-20260519-004: CSC_CL 页面画面过小

状态: Fixed
分类: Product Defect
页面: CSC_CL

## 问题描述

`CSC_CL` 页面主画面过小，视觉比例和其它客户演示页不一致。

2026-05-19 复查追加：当前 4K 改造后，`VI` 按 3840x2160 打开，但 `CSC_CL` 模块输入仍是前置 `RESIZE_RGA` 输出的 640x640；页面显示通道仍是 960x960 ARGB 局部 VO，界面结构和其它全屏客户页不一致。

## 大概解决方案

按统一页面模板重排 CSC_CL：扩大主画面区域，流程图和指标放到固定说明区；如果展示实时算法能力，输入优先提升到 4K，达不到时标注 1080P。

## 整改记录

CSC_CL 页面从 640x640 小窗口改为 `VI 640x640 -> CSC_CL -> OSD -> RGA 960x960 -> VO`，显示窗口居中放大到 960x960；流程图中标注当前实时输入和显示输出分辨率。

2026-05-19 临时修正：屏幕文案改为明确标注 `VI 3840x2160 -> RESIZE_RGA 640x640 -> CSC_CL -> OSD -> RGA 1080x1920 -> VO`，避免误导为 CSC_CL 模块 4K 输入。

2026-05-19 全屏整改：CSC_CL 最终显示从 960x960 局部 VO 改为 `RGA 1080x1920 ARGB -> VO` 全屏显示，ARGB stride 按 64 字节对齐为 4352；页面版式不再是局部小窗口。

2026-05-19 顺序整改：按复看要求改为 `VI 3840x2160 -> CSC_CL 4K -> RESIZE_RGA 1080x1920 -> OSD -> VO`，去掉 CSC_CL 前置缩放，算法先处理 4K，最后再为显示缩放。

## 验证

`timeout -s INT -k 5s 8s ./scripts/run_alldemo.sh --only CSC_CL` 运行成功，连接日志确认 `VI_0.output -> CSC_CL_76.input -> RESIZE_RGA_61.input0 -> OSD_81.input -> VO_0.input0`；兼容性检查显示 CSC_CL 输入为 `3840x2160 stride=3840 NV12`，CSC_CL 输出到 RESIZE_RGA 为 `3840x2160 stride=15360 ARGB`，RESIZE_RGA 输出到 OSD/VO 为 `1080x1920 stride=4352 ARGB`；`vi_frames/csc_frames/display_osd_frames` 增长到 `213/213/213`，退出时解绑和 pool 销毁完成。
