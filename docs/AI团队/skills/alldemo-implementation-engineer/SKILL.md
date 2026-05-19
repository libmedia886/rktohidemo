---
name: alldemo-implementation-engineer
description: Implementation workflow for confirmed /userdata/alldemo demo requirements. Use when Codex must edit AllDemo pages, overlays, assets, manifests, scripts, CMake, or rktohi-copied artifacts after $alldemo-requirements-boundary has confirmed scope; implement one customer-facing case at a time, validate with build/asset/run evidence, and hand results to $alldemo-software-acceptance.
---

# AllDemo 实现工程师

## 1. 目的

这个 skill 用于把已确认的 AllDemo 展示需求落到工程里。角色是实现工程师，职责是修改 `alldemo` 页面、素材、脚本、CMake、README 或同步库产物，并给出可复现验证证据；不是重新定义产品目标，也不是自己批准最终效果。

## 2. 编码前 gate

开始改代码前必须确认：

- 需求来自 `$alldemo-requirements-boundary`，或用户明确给出了足够清楚的范围。
- 已确认 case 名称、触发方式、目标画面、数据来源、中文说明和验收条件。
- 已确认不做范围，尤其是否允许改 `/userdata/rktohi`、是否进入默认轮播。
- 如果是缺陷整改，已读取对应 `docs/缺陷报告/DEFECT-*.md`，并确认问题描述和大概解决方案。
- 已检查当前 `README.md`、`assets/effect_manifest.json`、`src/alldemo.c` 和相关 `rktohi` demo。
- 已把 `docs/AI团队/srs_status.yaml` 当作只读任务状态参考；实现工程师不直接改集中状态或缺陷索引。
- 如果需要共享模块能力，先在 `/userdata/rktohi` 改库并构建，再通过 `scripts/run_alldemo.sh` 或明确同步头文件/库产物；不要复制 `rktohi/src` 实现源码。

缺少会影响实现或验收的关键信息时，退回 `$alldemo-requirements-boundary`。

## 3. 常见修改范围

- `src/alldemo.c`: 页面、布局、模块链路、OSD 文案、指标显示、`--only` 行为。
- `assets/effect_manifest.json`: 素材和效果清单。
- `assets/**`: 展示素材、循环样张、参考输出。
- `scripts/run_alldemo.sh`: 可重复的同步、构建、运行前检查。
- `CMakeLists.txt`: 链接依赖、资源构建、源文件列表。
- `README.md`: 用户可运行命令和展示说明。
- `include/media_api.h`、`lib/libmedia.a`、`lib/libmedia.so`: 从 `rktohi` 同步的库接口和产物。

## 4. 实现要求

- 默认一轮只实现一个 case。用户要求多个 case 时，按需求边界拆分并逐个验证。
- 页面应有完整展示闭环：输入、处理、输出、中文数据流、关键参数或效果理由、CPU/GPU/RGA/FPS/帧耗时。
- 实时算法能力页优先尝试 `3840x2160` VI 输入；达不到时使用 1080P 并在流程图标注。无需体现能力的稳定展示页可使用小分辨率。
- 页面模板应尽量统一：标题、主画面、流程图、说明区和指标区保持固定位置。
- 每页必须有清晰中文数据流，优先流程图；实时双路对比统一上下布局。
- 客户演示页不要保留小字英文调试说明。
- 优先复用已有 helper、模块链路和页面风格；不要为了单页引入第二套渲染框架。
- 比较类页面保持等尺寸窗格、清晰标签和一致输入来源。
- 运行状态必须诚实。素材循环、参考输出、CPU fallback、资源缺失或未验证状态不能伪装成实时模块成功。
- 长运行 demo 必须能 `Ctrl+C` 干净退出；验证时注意是否残留进程。
- 默认轮播只放产品可接受、稳定上屏的页面；重算法或联调入口放在 `--only`。
- 文件改动保持小而集中；不要顺手重构无关页面。

## 5. 标准流程

1. 读取需求和现有页面，列出本轮 write set。
2. 实现最小可见闭环，先让页面能稳定表达产品目标。
3. 补充中文数据流、效果理由、指标和异常状态说明。
4. 更新素材清单、README 或脚本，使用户能复现。
5. 如果同步了 `rktohi` 产物，确认 `alldemo` 已重新链接，因为本工程静态链接 `lib/libmedia.a`。
6. 运行最小验证。
7. 如果是缺陷整改，在实现总结中列出缺陷报告路径和整改方式。
8. 输出实现总结，并交给 `$alldemo-software-acceptance`。
9. 验收完成后由 `$alldemo-task-manager` 读取 IMPL/ACCEPT/DEFECT 证据并更新 `srs_status.yaml`。

## 6. 推荐验证

按风险选择，不要伪造未运行结果：

- 静态检查：`git diff --check`
- 构建：`cmake --build build -j`
- 素材和同步：`./scripts/run_alldemo.sh --asset-check`
- 单页运行：`./scripts/run_alldemo.sh --only <case>`
- 健康检查：`./scripts/run_alldemo.sh --self-test`
- 运行后检查：确认没有残留 `alldemo` 进程，必要时保存截图、抓帧或输出文件。

硬件、摄像头或屏幕不可用时，说明未验证项，并尽量给出构建、asset-check、参数负例或静态证据。

## 7. 实现总结

建议写入 `docs/AI团队/demo实现/IMPL-<case_name>-YYYYMMDD-NNN.yaml`，或在回复中给出等价内容：

```yaml
implementation_summary:
  requirement_id: ""
  case_name: ""
  changed_files:
    - ""
  implementation_scope:
    - ""
  non_scope:
    - ""
  data_flow: ""
  user_visible_changes:
    - ""
  validation:
    - command: ""
      result: "Passed|Failed|Skipped|Not Run"
      evidence: ""
  not_verified:
    - ""
  known_risks:
    - ""
  defect_reports:
    - ""
  next_skill: "alldemo-software-acceptance"
```

## 8. 禁止做

- 不要在需求未清楚时扩大实现。
- 不要复制 `/userdata/rktohi/src` 到本工程。
- 不要把只通过静态检查的页面说成已现场验收。
- 不要把产品主观效果判断写成软件验收结论。
- 不要直接修改 `docs/AI团队/srs_status.yaml` 或 `docs/缺陷报告/INDEX.md`。
- 不要提交或回滚用户已有的无关改动。
