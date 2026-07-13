# forest 专注森林 — 开题报告 (Proposal)

> **项目名称**：forest 专注森林（PC 桌面端）
> **开发者**：单人独立开发
> **技术栈**：C++17 + Qt 6.5 (QWidget) + Windows API + 定长二进制随机文件读写
> **开发平台**：Windows 10/11 x64

---

## 一、项目背景与动机

### 1.1 问题陈述

在数字化时代，个人电脑既是生产力工具，也是最大的干扰源。游戏、社交媒体、短视频平台随时可能打断深度工作状态，导致效率低下、任务拖延。移动端的"专注森林"类应用（如 Forest App）通过"种植虚拟树木"的游戏化机制成功帮助了数百万用户保持手机专注，但 PC 端的系统级专注工具却严重缺失——现有的番茄钟软件大多只是简单的计时器，缺乏激励机制和违规惩罚功能。

### 1.2 项目目标

本项目旨在开发一款运行在 Windows 平台上的桌面级专注效率工具，将"番茄工作法"与"模拟经营游戏"深度结合。用户通过设定专注时长来"种植"虚拟树木：专注成功则树木茁壮成长并汇入个人森林画布，中途分心（打开黑名单应用）则树木枯萎，形成视觉化惩罚。项目将采用 C++ 底层开发技术，满足高级语言程序设计课程的硬性学术指标。

### 1.3 技术选型理由

| 技术 | 选择理由 |
|------|----------|
| **C++17** | 课程要求语言；高性能、零运行时开销；丰富的标准库支持（`std::optional`、`std::fstream`） |
| **Qt 6.5 (QWidget)** | 成熟的跨平台桌面 GUI 框架；信号槽机制实现松耦合模块通信；`QPainter` 支持自定义绘制森林画布 |
| **Windows API** | 利用 `GetForegroundWindow()` + `QueryFullProcessImageNameW()` 实现系统级前台窗口监听，达成 PC 端反作弊 |
| **定长二进制文件** | 满足课程"随机文件读写"硬性要求；64 字节定长记录，通过 `seekg`/`seekp` 实现 O(1) 随机访问 |

### 1.4 职责分配

本项目为**单人独立开发**，承担全部职责：需求分析、系统架构设计、代码实现（C++ / Qt / WinAPI）、UI 设计、测试、文档撰写。采用自底向上的敏捷开发策略，先攻克底层存储引擎与系统监听，再集成 Qt UI 外壳。

---

## 二、系统功能概述

### 2.1 核心玩法

```
[选择植物与时长] → [开始专注(种子生成)] → [成功:树木成熟 + 金币] / [失败:树木枯萎]
                                                  ↓
                                          [森林画布历史回顾]
```

### 2.2 功能模块清单

| 模块 | 功能描述 |
|------|----------|
| **专注计时** | 支持 10~120 分钟番茄钟；植物经历种子→发芽→幼苗→茁壮→成熟 5 个生长阶段 |
| **植物体系** | 抽象基类 `AbstractPlant` → `Tree`/`Flower` → `OakTree`/`PineTree`/`Rose`，利用多态实现差异化生长 |
| **反作弊系统** | Windows API 轮询前台活跃窗口；用户配置进程黑白名单；违规立即触发枯萎惩罚 |
| **森林画布** | M×N 网格渲染历史专注记录；成功=绿色植物，失败/放弃=灰色枯木，形成视觉化时间足迹 |
| **金币商城** | 成功专注赚金币（5 分钟 = 1 金币）；消耗金币解锁新树种和白噪音背景 |
| **标签统计** | 每次专注可打标签（#学习、#写代码、#阅读、#运动）；饼图统计各标签时间分配 |
| **崩溃恢复** | 异常退出后重启自动检测，追加枯萎记录 |
| **自勉语录** | 专注中随机交替显示鼓励语句 |

---

## 三、技术架构设计

### 3.1 分层架构（MVC/Layered）

```
┌─────────────────────────────────┐
│     UI 表现层 (View)            │  MainWindow, GardenCanvas, TimerRing,
│                                 │  SettingsDialog, StoreDialog, TrayManager
├─────────────────────────────────┤
│     逻辑控制层 (Controller)     │  FocusController (状态机), AbstractPlant 继承体系,
│                                 │  CoinManager, QuoteProvider, TagManager
├─────────────────────────────────┤
│     系统与持久化层 (Model)      │  SystemMonitor (WinAPI), RuleEngine,
│                                 │  DatabaseManager (定长二进制文件)
└─────────────────────────────────┘
```

各层之间通过 Qt 信号槽机制松耦合通信，避免模块间的强引用关系。

### 3.2 核心类设计（≥5 类，≥2 层继承）

```
AbstractPlant (抽象基类)
  ├── Tree
  │     ├── OakTree
  │     └── PineTree
  └── Flower
        └── Rose

FocusController    DatabaseManager    SystemMonitor
RuleEngine         GardenCanvas       CoinManager
QuoteProvider      StoreDialog        MainWindow
```

**继承层次验证**：`AbstractPlant → Tree → OakTree` = 3 层 ≥ 2 层 ✓

### 3.3 数据存储设计

采用 **64 字节定长二进制记录**（`FocusRecord`），使用 `#pragma pack(push, 1)` 消除编译器对齐填充。每条记录包含：记录 ID、植物类型、计划/实际时长、时间戳、状态、违规次数、生长阶段、金币数、标签 ID。

**随机访问公式**：`Offset = recordId × 64 字节`，通过 `seekg`/`seekp` 实现 O(1) 随机读写与原地更新。

---

## 四、开发计划

采用 **5 周单人敏捷开发**，自底向上推进：

| 周次 | 阶段 | 核心产出 |
|:----:|------|----------|
| 第一周 | Model 层 | 定长二进制存储引擎 + 开题报告 |
| 第二周 | Logic 层 | OOP 植物继承体系 + 专注状态机 |
| 第三周 | System 层 | Windows API 系统监听 + 反作弊 + 崩溃恢复 |
| 第四周 | UI 层 | Qt GUI 界面 + 森林画布渲染 + 商城系统 |
| 第五周 | QA & Docs | 测试报告 + 结题报告 + 便携打包 |

详细任务分解见配套开发计划文档 `project_docs/forest_development_plan.md`。

---

## 五、关键技术难点与应对策略

| 难点 | 应对策略 |
|------|----------|
| `fstream` 双向读写时的 EOF 状态锁死 | 每次模式切换前强制 `file_.clear()` 清除状态位 |
| 管理员权限窗口无法获取进程名 | 使用 `PROCESS_QUERY_LIMITED_INFORMATION` 替代 `PROCESS_QUERY_INFORMATION` |
| 结构体对齐差异破坏定长记录 | `#pragma pack(push, 1)` + `static_assert(sizeof(FocusRecord)==64)` 双保险 |
| 虚析构函数缺失导致内存泄漏 | `AbstractPlant` 显式声明 `virtual ~AbstractPlant() = default` |
| 绝对路径导致跨机器崩溃 | 统一使用 `QCoreApplication::applicationDirPath()` 动态获取 exe 目录 |

---

## 六、预期成果

1. 完整的 Windows 桌面端专注效率工具（`.exe` 绿色便携包）
2. 源代码不少于 2000 行（含 Doxygen 规范注释）
3. 开题报告（本文）不少于 1000 字
4. 结题报告不少于 3000 字
5. 测试报告覆盖 ≥15 个测试用例（含边界/健壮性测试）
6. 满足课程全部硬性学术指标（5 类、2 层继承、定长二进制随机读写、Windows API 调用、双文档）
