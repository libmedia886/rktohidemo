# DEFECT-CUSTOMER-DEMO-20260519-006: 数据流说明不清晰

状态: Fixed
分类: Product Defect
范围: 默认轮播

## 问题描述

部分页面的数据流说明不够清晰，客户难以理解输入、处理模块和输出之间的关系。

## 大概解决方案

每页增加统一流程图，至少包含输入源、分辨率、核心模块、后处理和 VO 输出。流程图使用中文为主，模块名保留必要缩写。

## 本轮进展

已为 VMIX、OSD、RGA、CSC_CL 增加中文流程图和分辨率说明；公共单模块页面的底部数据流说明已补齐分辨率和关键链路，并覆盖当前模块页清单中的 WBC、CAP_DEHAZE、DCP_FAST_DEHAZE、CLAHE、RETINEX、CONV_CL、TRANSFORM、EDOF_CL、MCF_FUSION_CL、PANO 等页面。

## 验证证据

- `grep` 检查 CAP_DEHAZE/DCP_FAST_DEHAZE 文档中已无左右对比旧描述。
- `cmake --build build -j4` 通过。
- CAP_DEHAZE 与 RETINEX 短运行生成 1080x1920 VO 抓帧，页面使用更新后的数据流说明。
