# DEFECT-CUSTOMER-DEMO-20260519-008: 数据流仍未清楚说明4K输入、处理顺序和缩放位置

状态: In Progress
分类: Product Defect
范围: 默认轮播实时摄像头页

## 问题描述

用户复看后指出，部分页面虽然已经切到 3840x2160 实时摄像头输入，但屏幕数据流仍可能写成 640x640，或没有清楚说明“4K 输入、图像处理模块、显示缩放”之间的先后关系。

## 期望行为

- 需要展示图像处理能力的页面，优先表达并实现“VI 3840x2160 输入 -> 图像处理模块 -> 最后为显示缩放 -> VO”。
- 如果当前模块因性能、格式、buffer 流转或视觉清晰度原因必须先缩放再处理，页面必须明确标注缩放发生的位置和模块实际工作分辨率。
- 数据流说明要让非工程人员一眼区分：摄像头输入分辨率、处理模块工作分辨率、最终显示分辨率。

## 已知当前状态

- OSD 已改为 `VI 3840x2160 -> RESIZE_RGA 1080x608 -> OSD -> VMIX -> 页面OSD -> VO`，单个实时主画面宽度统一为 1080；CSC_CL 已改为 `VI 3840x2160 -> CSC_CL 4K -> RESIZE_RGA 1080x1920 -> OSD -> VO`。
- CLAHE 已改为 `VI 3840x2160 -> CLAHE passthrough/增强 -> RESIZE_RGA 1080x608 -> VMIX_RGA -> OSD/VO`，不再用 VPSS 分路，后置缩放保持 16:9 比例；验收要看 CLAHE 帧计数、passthrough 0/1 切换和后置 RESIZE_RGA 帧计数。
- VPSS、VMIX、RETINEX、TRANSFORM 已按“先处理4K，再为显示缩放”方向调整；RETINEX 已改为 CLAHE 风格单路 1 秒直通/1 秒增强，避免 VPSS 双路 4K 对比造成卡顿。验收要看算法帧计数和后置 RESIZE_RGA 帧计数是否同时增长。
- TRANSFORM 4K 四路链路受 256MB media pool 限制，当前使用最小 4K 中间池，运行时可能出现 VPSS 输出池紧张日志；若后续要提升帧率，需要继续做池预算或降低分支并发。
- CAP_DEHAZE、CONV_CL 当前从 4K 帧取样后在 CPU/staging 路径进入算法模块。

## 下一步

继续复查 OSD、CSC_CL、CAP_DEHAZE、CONV_CL 等例外页；已调整页面继续用下游 buffer 计数验证，而不是只看 VI 是否出帧。
