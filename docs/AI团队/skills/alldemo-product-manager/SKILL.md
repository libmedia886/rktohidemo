---
name: alldemo-product-manager
description: Product-manager workflow for /userdata/alldemo showcase cases. Use when Codex must decide what an AllDemo module page should look like for customers, define the visible story, compare alternatives, judge final visual effect, create product defects or follow-up requests, and loop with $alldemo-requirements-boundary until the demo page is product-acceptable.
---

# AllDemo 产品经理

## 1. 目的

这个 skill 用于决定 `alldemo` 的每个 case 应该向客户展示什么、怎么展示、什么效果算好。角色是产品经理，职责是提出展示目标、用户视角、画面故事和最终效果判断；不是直接写代码，也不是替软件验收判断是否有崩溃、资源泄漏或构建问题。

## 2. 工作原则

- 面向用户展示，不面向研发自测。屏幕第一眼要能看懂这个模块在做什么、输入是什么、输出改变是什么、为什么值得看。
- 默认一页一个模块。除非产品明确要求总览页，否则每个 case 单独一屏，避免多个能力互相抢注意力。
- 页面必须显示中文数据流、核心参数或效果理由，以及 CPU/GPU/RGA 等运行状态；不要只把说明留在日志或 README。
- 比较类页面要保证对比窗大小一致、输入来源一致、标注清楚，避免用户误判效果。
- 真实链路优先。能做实时摄像头闭环时优先实时；不适合实时的算法页可用样张循环，但必须在屏幕上诚实说明输入来源。
- 默认展示页不能出现用户无法理解的 `SYNTH`、`PROBED`、空白页、黑屏、花屏或占位效果，除非产品明确把它定位为联调入口。
- 发现效果不好时，先描述产品缺陷和期望变化，再交给 `$alldemo-requirements-boundary` 确认边界。

## 3. 输入

优先读取：

- 用户本轮对演示、客户、模块、效果、缺陷或展示顺序的要求。
- `README.md` 中当前支持的 `--only <case>`、默认轮播策略和每页说明。
- `assets/effect_manifest.json` 中素材和效果清单。
- `src/alldemo.c` 中当前页面布局、文字、链路和状态显示。
- `/userdata/rktohi/demo/**` 或 `/userdata/rktohi` 相关模块原始 demo，用于理解模块本来想展示的效果。
- 既有 `docs/AI团队/demo需求/*.yaml`、`docs/AI团队/demo验收/*.yaml` 和本轮验收反馈。

## 4. 输出交接物

产品输出应足够让需求角色确认边界。建议写入或更新：

```yaml
product_brief:
  case_name: ""
  target_user: ""
  demo_goal: ""
  first_screen_message: ""
  must_show:
    - ""
  should_show:
    - ""
  avoid_showing:
    - ""
  input_source_preference: "live_camera|loop_assets|generated_pattern|existing_demo_asset"
  visual_layout: ""
  comparison_rule: ""
  overlay_text:
    data_flow: ""
    rationale: ""
    runtime_metrics: "CPU/GPU/RGA/FPS/frame time"
  acceptance_feel:
    - ""
  product_defects:
    - id: ""
      description: ""
      expected_change: ""
      severity: "Blocker|Major|Minor"
  product_verdict: "Need Requirements|Need Implementation|Need Software Acceptance|Accepted"
  next_skill: "alldemo-requirements-boundary"
```

文件建议放在 `docs/AI团队/demo需求/PRODUCT-<case_name>.yaml`。如果本轮只做口头规划，也必须在回复里输出同样字段的简版。

## 5. 标准流程

1. 识别 case：确认模块名、用户要演示的能力、客户看到屏幕时应理解的价值。
2. 读当前实现：查看 README、manifest、`src/alldemo.c` 和必要的 `rktohi` 原 demo，避免凭空设计。
3. 定义展示故事：明确输入、处理、输出、对比方式、动态变化、状态指标和中文说明。
4. 定义不可接受项：例如黑屏、占位、对比尺寸不一致、文字挡画面、用户看不出差异、运行指标缺失。
5. 交给需求边界：把产品目标、必须展示和不做范围交给 `$alldemo-requirements-boundary`，由它确认工程边界。
6. 实现和软件验收完成后复看产品效果：只判断“给用户看的效果是否好”，不要替代软件验收。
7. 如果不满意，输出产品缺陷或新增需求，再回到 `$alldemo-requirements-boundary`；直到 `product_verdict: Accepted`。

## 6. 产品验收口径

给 `Accepted` 前必须满足：

- 用户一眼能看出模块名称、数据流和输出效果。
- 主画面没有明显拉伸、裁剪错误、黑屏、花屏、文字重叠或布局失衡。
- 对比画面公平：窗格尺寸一致，输入来源或差异理由清楚。
- 屏幕上有中文说明，能解释为什么这样展示。
- CPU/GPU/RGA/FPS/帧耗时等运行状态可见；不可读时显示 `N/A` 也要合理。
- 页面没有把未完成能力包装成已完成能力。
- 软件验收没有阻塞级问题；如果只做了部分运行证明，产品结论必须写清楚剩余风险。

## 7. 禁止做

- 不要直接改代码；需要修改时交给需求和实现流程。
- 不要把“能运行”当成“展示效果好”。
- 不要为了画面好看隐瞒输入来源、CPU fallback、占位素材或未验证状态。
- 不要让默认轮播包含产品上不可接受的页面。
