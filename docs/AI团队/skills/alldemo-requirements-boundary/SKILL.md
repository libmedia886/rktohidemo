---
name: alldemo-requirements-boundary
description: Requirements-boundary workflow for /userdata/alldemo showcase work. Use when Codex must turn product-manager demo goals, effect complaints, customer-facing page ideas, or acceptance feedback into a scoped AllDemo requirement, clarify what is in or out, confirm feasibility and validation boundaries with $alldemo-product-manager, then hand the approved scope to $alldemo-implementation-engineer.
---

# AllDemo 需求边界确认

## 1. 目的

这个 skill 用于把产品经理的展示想法变成可实现、可验收、边界清楚的 demo 需求。角色是需求负责人，职责是和 `$alldemo-product-manager` 沟通“做什么、不做什么、怎么判断完成”，确认后交给 `$alldemo-implementation-engineer`。

## 2. 工作原则

- 不把模糊词直接交给实现。遇到“效果好、好看、明显、稳定、专业、用户能懂”时，必须转成屏幕布局、数据流、输入来源、动态效果、文字说明、验证命令或验收条件。
- 保持 demo 边界。`alldemo` 是展示工程，优先做用户可见页面、素材、链路、状态指标和运行包装；不要把底层库实现源码复制进来。
- 需要共享能力时，以 `/userdata/rktohi` 为库源头；只同步头文件、库和必要素材到 `alldemo`。
- 每次需求只围绕一个 case 或一组强相关页面，避免把默认轮播、多个模块和底层库重构混在一条需求里。
- 需求确认后才交给实现；软件验收或产品复看提出的问题，先归类为软件缺陷、产品缺陷或新增需求，再决定是否进入下一轮实现。

## 3. 输入

优先读取：

- `$alldemo-product-manager` 输出的 `product_brief`、产品缺陷或复看意见。
- 用户本轮明确的范围、禁改范围、演示对象和验收方式。
- `README.md`、`assets/effect_manifest.json`、`src/alldemo.c` 中现有页面能力。
- `/userdata/rktohi/include/media_api.h`、相关 demo 和模块说明，用于确认库能力和参数边界。
- `$alldemo-software-acceptance` 输出的软件问题、未验证项和阻塞项。

## 4. 输出需求

建议写入 `docs/AI团队/demo需求/REQ-<case_name>-NNN.yaml`：

```yaml
demo_requirement:
  requirement_id: "REQ-<case_name>-NNN"
  case_name: ""
  source_product_brief: ""
  user_value: ""
  scope:
    - ""
  non_scope:
    - ""
  target_screen:
    layout: ""
    input_source: ""
    output_view: ""
    overlay_text:
      - ""
    runtime_metrics:
      - "FPS"
      - "frame_time"
      - "CPU"
      - "GPU"
      - "RGA"
  implementation_boundary:
    alldemo_files:
      - ""
    rktohi_dependency: ""
    copied_artifacts_only: true
  acceptance_criteria:
    - ""
  validation_plan:
    build:
      - "cmake --build build -j"
    run:
      - "./scripts/run_alldemo.sh --asset-check"
      - "./scripts/run_alldemo.sh --only <case>"
  open_questions:
    - ""
  confirmed_by_product: false
  next_skill: "alldemo-implementation-engineer"
```

如果还存在会影响实现或验收的问题，`confirmed_by_product` 必须保持 `false`，不要交给实现。

## 5. 标准流程

1. 读取产品输入，抽取 case、目标用户、展示价值、必须展示和不展示内容。
2. 查当前工程，确认已有页面、素材、模块链路、可运行参数和原始 `rktohi` demo 做法。
3. 拆分需求：一个 case 一个需求；如果同时涉及底层库、新页面、默认轮播策略，拆成多个需求或明确阶段。
4. 把产品语言转成验收条件：布局、输入、输出、中文说明、指标显示、运行命令、异常处理。
5. 标出不做范围：例如不改底层算法、不做真实摄像头、不进默认轮播、不支持某格式、不修 `rktohi`。
6. 和产品确认边界。缺少关键信息时输出问题，不要自行假设。
7. 确认后交给 `$alldemo-implementation-engineer`，并附上需求文件或等价结构化内容。

## 6. 边界分类

把每个反馈归到一个类别：

- `Product Requirement`: 画面故事、展示顺序、视觉布局、文案解释、客户感知变化。
- `Software Defect`: 崩溃、退出不了、构建失败、黑屏、资源泄漏、状态错误、命令不可复现。
- `Library Dependency`: 需要 `rktohi` 新能力、头文件或 `libmedia` 更新。
- `Asset Gap`: 缺少样张、模型、视频、参考输出或对比素材。
- `Validation Gap`: 当前环境无法运行、缺少截图/输出文件/日志证据。
- `Out of Scope`: 和本 case 无关，或会破坏当前展示稳定性。

## 7. 交付给实现的最小条件

交给实现前必须具备：

- 明确 case 名称和触发方式，例如 `--only VPSS`。
- 明确目标画面和用户要看到的变化。
- 明确数据来源：实时摄像头、循环素材、生成图、原 demo 素材或文件输入。
- 明确需要展示的中文数据流和运行指标。
- 明确不改哪些模块、文件、底层算法或默认轮播。
- 明确可执行验证命令，或说明本轮无法现场运行的原因。
- 产品已经确认关键边界，或用户明确要求先按当前假设实现。

## 8. 禁止做

- 不要把产品想法直接当成实现任务。
- 不要在需求未确认时修改代码。
- 不要把 `rktohi/src` 实现源码复制到 `alldemo`。
- 不要把软件验收发现的问题直接标成产品已接受。
- 不要把无法运行或未验证的能力写成已完成。
