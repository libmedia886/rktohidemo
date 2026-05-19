# DEFECT-CUSTOMER-DEMO-20260519-002: OSD VI OSD 页面显示混乱

状态: Fixed
分类: Product Defect
页面: OSD

## 问题描述

`OSD VI OSD` 页面信息堆叠较乱，小字英文调试说明对客户没有帮助，影响观看重点。

## 大概解决方案

重新整理 OSD 页面层级：保留主画面、少量中文说明、流程图和关键指标；删除小字英文说明，把必要的 bind 信息改成简短中文流程。

## 整改记录

删除页面底部端口级英文 bind 调试串，改为中文流程图、中文动态叠加说明和中文帧计数；目标框、状态条、告警块说明改为客户可读文案。

## 验证

`timeout -s INT -k 5s 12s ./scripts/run_alldemo.sh --only OSD` 运行成功，生成 `vo_captures/osd_vo_000150.bmp` 和 `vo_captures/osd_vo_000300.bmp`，退出后资源释放正常。
