# 异色发掘场景化 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 将异色发掘改为克制、有机、可验证的卡通发掘场景和图鉴网格。

**Architecture:** 在 `CommercePages.cpp` 内增加只负责绘制的 `GachaDigSiteWidget`，由现有 `GachaPageController` 提供展示状态；业务仍由 `GachaManager` 和 `CoinManager` 完成。收藏区继续动态刷新，但由纵向行改为四列卡片网格。

**Tech Stack:** C++17、Qt 6 Widgets、QPainter、QSS、Qt Test。

## Global Constraints

- 不改变抽取概率、价格、返还、存储格式和成就触发。
- 文案直接叙述，不使用安抚、拟人化或过度情绪化表达。
- 不新增外部图片依赖，不修改无关页面。

---

### Task 1: 场景与布局回归

**Files:**
- Modify: `src/tests/UiSmokeTest.cpp`

- [ ] 增加场景控件、图鉴网格和直接文案断言。
- [ ] 保存发掘前、发掘后及三种窗口尺寸截图。
- [ ] 运行 UI smoke，确认新断言因当前通用面板实现而失败。

### Task 2: 发掘场景与图鉴网格

**Files:**
- Modify: `src/ui/CommercePages.cpp`
- Modify: `src/ui/CommercePages.h`

- [ ] 实现带不规则土层、石块、根系和光迹的 `GachaDigSiteWidget`。
- [ ] 重组左侧信息与操作层级，并把结果状态同步给场景控件。
- [ ] 将收藏列表替换为四列图鉴卡片，保持锁定信息最小化。
- [ ] 运行 UI smoke，确认断言通过。

### Task 3: 视觉迭代与发布验证

**Files:**
- Modify: `AGENTS.md`（仅长期视觉规则）
- Modify: `MEMORY.md`

- [ ] 检查三种基准分辨率和发掘后截图，修正裁剪、密度与层次问题。
- [ ] 运行 Release 构建和完整 CTest 6/6。
- [ ] 更新协作文档，部署根目录程序并完成启动烟雾测试。
