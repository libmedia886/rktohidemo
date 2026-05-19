# DEFECT-CUSTOMER-DEMO-20260519-007: 实时对比布局不统一

状态: Fixed
分类: Product Defect
范围: 实时对比页

## 问题描述

实时算法对比页面有的使用上下对比，有的使用左右对比，观看习惯不一致。

## 大概解决方案

统一实时双路对比为上下布局：上方显示输入或基准，下方显示处理结果；流程图中标注输入分辨率和算法链路。非实时多图样张页可单独说明布局原因。

## 整改记录

已将公共实时对比绘制函数改为上下布局，CAP_DEHAZE/DCP_FAST_DEHAZE 复用该布局；CLAHE/RETINEX 绑定页原有上下布局保持一致。同步更新 CAP_DEHAZE 产品 brief、SRS、README 和 effect manifest，避免文档仍描述左右对比。

## 验证证据

- `cmake --build build -j4` 通过。
- `timeout -s INT -k 5s 12s ./scripts/run_alldemo.sh --only CAP_DEHAZE` 生成 `vo_captures/cap_dehaze_vo_000150.bmp`。
- `timeout -s INT -k 5s 12s ./scripts/run_alldemo.sh --only RETINEX` 生成 `vo_captures/retinex_vo_000150.bmp` 和 `vo_captures/retinex_vo_000300.bmp`。
