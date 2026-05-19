# DEFECT-CUSTOMER-DEMO-20260519-004: CSC_CL 页面画面过小

状态: Fixed
分类: Product Defect
页面: CSC_CL

## 问题描述

`CSC_CL` 页面主画面过小，视觉比例和其它客户演示页不一致。

## 大概解决方案

按统一页面模板重排 CSC_CL：扩大主画面区域，流程图和指标放到固定说明区；如果展示实时算法能力，输入优先提升到 4K，达不到时标注 1080P。

## 整改记录

CSC_CL 页面从 640x640 小窗口改为 `VI 640x640 -> CSC_CL -> OSD -> RGA 960x960 -> VO`，显示窗口居中放大到 960x960；流程图中标注当前实时输入和显示输出分辨率。

## 验证

`timeout -s INT -k 5s 12s ./scripts/run_alldemo.sh --only CSC_CL` 运行成功，连接日志确认 RGA 输出 960x960 ARGB 到 VO，`vo_captures/csc_cl_vo_000300.bmp` 文件头确认为 960x960，退出时所有 pool 正常销毁。
