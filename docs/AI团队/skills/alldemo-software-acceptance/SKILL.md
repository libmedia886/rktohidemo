---
name: alldemo-software-acceptance
description: Software acceptance workflow for /userdata/alldemo showcase changes. Use after $alldemo-implementation-engineer finishes a demo case, or when Codex must inspect an AllDemo page for build failures, runtime crashes, bad shutdown, black screen, layout/overlay bugs, missing assets, wrong linkage, validation gaps, and software defects before $alldemo-product-manager reviews final effect.
---

# AllDemo 软件验收

## 1. 目的

这个 skill 用于在实现完成后检查软件问题。角色是验收工程师，职责是找构建、运行、资源、退出、画面、指标、链路和验证证据上的问题；不是重新定义产品目标，也不是替产品判断“够不够好看”。

## 2. 验收原则

- 先看软件可用性，再交给产品看效果。
- 结论必须基于证据：命令、日志、截图/抓帧、输出文件、源码行或明确的未验证原因。
- 对硬件相关页面，没跑就不能说通过；可以给出构建通过和现场未验证的边界。
- 问题要能交回责任角色：实现问题交 `$alldemo-implementation-engineer`，需求不清交 `$alldemo-requirements-boundary`，展示效果不满意交 `$alldemo-product-manager`。
- 长运行 demo 的干净退出属于验收内容。
- 缺陷整改必须按 `docs/缺陷报告/DEFECT-*.md` 逐项复验；一个缺陷一个结论，不要把多个缺陷合并验收。

## 3. 输入

优先读取：

- `$alldemo-implementation-engineer` 的实现总结、changed files 和验证命令。
- `$alldemo-requirements-boundary` 的需求边界和验收条件。
- 本轮 git diff、`README.md`、`scripts/run_alldemo.sh`、`src/alldemo.c`、`assets/effect_manifest.json`。
- 相关 `rktohi` 产物同步状态：`include/media_api.h`、`lib/libmedia.a`、`lib/libmedia.so`。
- `docs/AI团队/srs_status.yaml`，只读，用于确认当前 SRS 状态和关联缺陷。

## 4. 验收检查项

必须按适用范围检查：

- 构建：是否能重新编译，静态链接库是否已重新链接。
- 资源：素材路径、图片/视频解码、模型、参考输出是否存在。
- 单页入口：`--only <case>` 是否能启动目标页面，是否误初始化其它重模块。
- 画面基本正确：无黑屏、花屏、严重拉伸、对比窗大小不一致、文字重叠、状态栏遮挡主画面。
- 画面一致性：标题、主画面、流程图、说明区和指标区是否符合统一模板。
- 输入分辨率：实时算法页是否优先使用 4K；未达到 4K 时是否在流程图标注 1080P 或实际分辨率。
- 链路显示：屏幕数据流是否和实际模块链路一致，是否足够清晰。
- Buffer 流转：不能只看模块创建、bind 成功或最终截图；必须检查输入到下游模块的 buffer 是否实际流转，例如 VI/VPSS/RGA/RESIZE_RGA/VMIX/OSD/VO 等相关模块帧计数、输出队列或日志计数是否沿链路增长。若 buffer 没有流转，模块没有可处理数据，不能判定页面通过。
- 对比布局：实时双路对比是否统一为上下布局。
- 指标显示：FPS、帧耗时、CPU/GPU/RGA 状态是否存在；不可读时是否合理显示 `N/A`。
- 退出：`Ctrl+C` 或 timeout 后是否干净退出，无残留进程。
- 默认轮播：改动是否意外把不稳定页、占位页或联调页放入默认展示。
- 同步：如果 `rktohi` 库更新，`alldemo` 头文件、静态库、动态库和可执行文件是否一致。
- 回归风险：本轮改动是否影响无关 case、公共 helper、脚本参数或构建依赖。

## 5. 推荐命令

按需求选择最小足够集合：

```bash
git diff --check
cmake --build build -j
./scripts/run_alldemo.sh --asset-check
./scripts/run_alldemo.sh --only <case>
./scripts/run_alldemo.sh --self-test
```

运行单页时建议设置短时间窗口并检查残留进程。命令失败时保留错误摘要，不要只写“失败”。

对于 bind 链路页面，运行日志里应记录关键模块的帧计数或等价 buffer 流转证据。例如 `VI input=3840x2160 display=1080x608 vi_frames=... resize_frames=...` 中，`resize_frames` 跟随 `vi_frames` 增长才证明 VI buffer 已经送到 RESIZE_RGA 并被下游处理；如果只有 VI 计数增长而下游计数为 0，应按链路阻塞或 buffer 未流转处理。

## 6. 缺陷记录

验收发现新缺陷时，先在 `docs/缺陷报告/` 写一缺陷一文件的简洁报告。缺陷报告至少包含状态、分类、页面或范围、问题描述和大概解决方案。

同时建议写入 `docs/AI团队/demo验收/ACCEPT-<case_name>-YYYYMMDD-NNN.yaml`：

```yaml
acceptance_report:
  case_name: ""
  requirement_id: ""
  verdict: "Pass|Pass With Risks|Fail|Blocked"
  evidence:
    - command: ""
      result: "Passed|Failed|Skipped|Not Run"
      summary: ""
  software_issues:
    - id: "BUG-<case_name>-NNN"
      severity: "Blocker|Major|Minor"
      category: "Build|Runtime|Asset|Layout|Overlay|DataFlow|Metrics|Shutdown|Linkage|Regression|Validation"
      description: ""
      evidence: ""
      defect_report: "docs/缺陷报告/DEFECT-<case>-YYYYMMDD-NNN-<slug>.md"
      owner_skill: "alldemo-implementation-engineer|alldemo-requirements-boundary|alldemo-product-manager"
      required_action: ""
  not_verified:
    - ""
  next_skill: "alldemo-product-manager"
```

`Pass` 只能用于适用检查都通过且无阻塞未验证项。存在现场未跑、硬件不可用或只能静态确认时，最多给 `Pass With Risks` 或 `Blocked`。

验收报告写完后，不直接改 `docs/AI团队/srs_status.yaml`；由 `$alldemo-task-manager` 根据 ACCEPT 文件更新集中状态。

## 7. 判定标准

- `Pass`: 构建、资源、单页运行、画面基本正确、指标显示、退出和需求验收命令均通过。
- `Pass With Risks`: 核心软件检查通过，但存在非阻塞未验证项，例如没有现场屏幕截图。
- `Fail`: 发现可复现软件缺陷，必须回到实现修复。
- `Blocked`: 环境、硬件、素材、依赖或需求边界缺失导致无法验收。

## 8. 交接规则

- `Fail` 且 owner 是实现：交给 `$alldemo-implementation-engineer`。
- `Blocked` 且原因是需求不清：交给 `$alldemo-requirements-boundary`。
- 软件通过但展示不够好：交给 `$alldemo-product-manager` 复看并提产品缺陷。
- 产品接受后，本轮闭环完成。

## 9. 禁止做

- 不要因为构建通过就给整页验收通过。
- 不要把产品主观不满意写成软件 bug，除非有明确的软件表现问题。
- 不要修代码，除非用户明确要求“验收并修复”。
- 不要把未运行的硬件链路写成已通过。
- 不要直接修改 `docs/AI团队/srs_status.yaml` 或 `docs/缺陷报告/INDEX.md`。
