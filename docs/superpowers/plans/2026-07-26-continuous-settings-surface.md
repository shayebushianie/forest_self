# 连续森林画布设置页 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 将设置页收敛为连续森林画布、轻量分类导航、单一内容表面和弱边界控件。

**Architecture:** 保留 `SettingsPage` 的控件、信号和服务边界，只调整设置页专用层级与 QSS。`settingsGlassShell` 成为唯一内容表面，其子层透明；现有 `ChoiceRow` 继续统一承载整行选择行为。

**Tech Stack:** C++17、Qt 6 Widgets、QSS、QTest、CMake/CTest。

## Global Constraints

- 不修改偏好字段、数据格式、页面索引、即时保存或业务服务。
- 不新增动画；减少动效和高对比偏好继续生效。
- 保留当前工作树全部既有修改，不提交、不推送、不重新打包。

---

### Task 1: 锁定连续表面与整行选择行为

**Files:**
- Modify: `src/tests/UiSmokeTest.cpp`

**Interfaces:**
- Consumes: `SettingsPage` 现有对象名和 `ChoiceRow` 行为。
- Produces: `settingsPageUsesOneContinuousSurface()` 回归测试。

- [ ] **Step 1: 写入失败测试**

为 `settingsGlassShell`、`settingsRail`、`settingsContentPanel` 和所有
`settingsListGroup` 分别执行仅背景渲染；断言只有外壳产生不透明表面。
依次点击 `focusPlantRow`、`focusModeRow`、`focusTagRow` 的文字区域，
断言对应组合框弹出视图可见。

- [ ] **Step 2: 运行红灯**

Run:
`build-ui-simple\forest_ui_smoketest.exe settingsPageUsesOneContinuousSurface`

Expected: FAIL，因为当前分类栏、内容面板和分组仍绘制独立不透明背景。

- [ ] **Step 3: 保持测试不依赖颜色常量**

测试只判断独立表面是否存在及交互是否打开正确列表，不绑定具体 RGB，
避免字体和 GPU 差异造成像素金图式脆弱性。

### Task 2: 实现连续画布与弱边界

**Files:**
- Modify: `src/ui/AppStyle.cpp`
- Modify: `src/ui/SettingsPage.cpp`

**Interfaces:**
- Consumes: 现有动态属性和对象名。
- Produces: 单一外壳表面、透明子层、浅色分类导航、弱边界输入控件。

- [ ] **Step 1: 最小化视觉层级**

将 `settingsRail`、`settingsContentPanel`、`settingsListGroup` 设为透明无框；
为 `settingsGlassShell` 提供统一浅森林色表面和单一圆角边界。

- [ ] **Step 2: 调整导航与分组**

把分类图标改为深森林色，导航默认透明，hover 使用轻量森林色，
选中态使用低对比浅绿填充和左侧短强调线；分组仅保留标题、间距和分隔线。

- [ ] **Step 3: 弱化重复控件边界**

组合框、数值框和输入框默认使用浅色填充与透明边界；hover 显示弱边界，
focus 显示 2px 可见焦点环。`SproutToggle` 不增加外层边框。

- [ ] **Step 4: 运行目标测试**

Run:
`ctest --test-dir build-ui-simple -R forest_ui_smoketest --output-on-failure`

Expected: PASS。

### Task 3: 视觉与发布验证

**Files:**
- Modify: `AGENTS.md`
- Modify: `MEMORY.md`

**Interfaces:**
- Consumes: UI smoke 截图输出和构建产物。
- Produces: 三分辨率视觉证据、部署程序及动态项目记录。

- [ ] **Step 1: 生成并检查截图**

检查四分类在 1366×768、1440×900、1920×1080 下的截图，并检查
125% 字号和高对比状态；重点确认连续背景、控件焦点、滚动和裁剪。

- [ ] **Step 2: 完整验证**

Run:
`cmake --build build-ui-simple --config Release --parallel 4`

Run:
`ctest --test-dir build-ui-simple --output-on-failure`

Expected: 6/6 PASS。

- [ ] **Step 3: 部署与记录**

将通过测试的 `build-ui-simple/forest.exe` 复制到工作区根目录，
核对 SHA-256 并完成三秒响应检查。仅在 `AGENTS.md` 更新长期设置页规则，
在 `MEMORY.md` 记录实际改动、验证与剩余风险。
