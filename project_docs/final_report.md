# forest 专注森林 — 结题报告 (Final Report)

## 摘要

forest 专注森林是一款运行在 Windows 平台的桌面级专注效率工具，将"番茄工作法"与"模拟经营游戏"深度结合。用户设定专注时长种植虚拟树木：专注成功则树木茁壮成长汇入个人森林画布，中途分心（打开黑名单应用）则树木枯萎形成视觉化惩罚。本软件采用 C++17 + Qt 6.5 + Windows API 技术栈开发，实现定长二进制随机文件读写、三层 OOP 继承体系、系统级前台窗口监听反作弊、金币商城、崩溃恢复等完整功能，源代码超过 2000 行。

---

## 一、需求分析

### 1.1 用户需求

在数字化时代，PC 端的游戏、社交媒体、短视频是最主要的专注杀手。用户需要在电脑上获得与移动端 Forest App 类似的"种树防分心"体验，并通过系统级反作弊机制确保有效性。

### 1.2 核心功能需求

- 可配置的番茄钟计时（10~120 分钟）
- 多种可选植物（橡树、松树、玫瑰等），多态差异化生长
- Windows 系统级前台窗口监听与黑白名单反作弊
- 专注历史森林画布可视化（成功绿植 / 失败枯木同框）
- 金币激励系统与植物商城
- 崩溃恢复与异常退出检测
- 自勉语录随机展示

### 1.3 学术指标要求

| 指标 | 要求 | 达标情况 |
|------|------|:--:|
| 代码行数 | ≥2000 行 | ~2000+ 行 |
| 类数量 | ≥5 个 | 17+ 个 |
| 继承层次 | ≥2 层 | 3 层（AbstractPlant→Tree→OakTree） |
| 定长二进制随机读写 | seekg/seekp 实现 | DatabaseManager 完整实现 |
| Windows API 调用 | ≥2 个 | GetForegroundWindow + QueryFullProcessImageNameW + FlushFileBuffers |
| 开题报告 | ≥1000 字 | ~1800 字 |
| 结题报告 | ≥3000 字 | 本文 |
| 测试报告 | ≥15 用例 | 29 个测试用例 |

---

## 二、系统架构设计

### 2.1 分层架构

```
┌─────────────────────────────────┐
│     UI 表现层                    │  MainWindow, GardenCanvas, TimerRing,
│                                  │  SettingsDialog, StoreDialog
├─────────────────────────────────┤
│     逻辑控制层                   │  FocusController (状态机),
│                                  │  AbstractPlant 继承体系 (5 个具体类),
│                                  │  CoinManager, QuoteProvider
├─────────────────────────────────┤
│     系统与持久化层               │  SystemMonitor (WinAPI 轮询),
│                                  │  RuleEngine (黑白名单),
│                                  │  DatabaseManager (64 字节定长记录)
└─────────────────────────────────┘
```

层间通过 Qt 信号槽机制松耦合通信。`FocusController::tick()` 每秒推进状态机，`SystemMonitor::sig_violationDetected()` 触发违规惩罚，所有状态变更通过 `DatabaseManager` 持久化到 `sessions.dat`。

### 2.2 OOP 继承体系

```
AbstractPlant (抽象基类，纯虚 grow/wither)
  ├── Tree (height_, fruitCount_)
  │     ├── OakTree (30%/60%/100% 生长阈值)
  │     └── PineTree (10%/55%/100% 快速发芽)
  └── Flower (petalCount_, bloomColor_)
        └── Rose (枯萎时花瓣归零)
```

`FocusController` 通过 `std::unique_ptr<AbstractPlant>` 管理当前植物，切换时自动释放旧对象，避免内存泄漏。状态机使用 `enum class State : uint32_t` 强类型枚举。

### 2.3 核心类清单

| 类名 | 文件 | 职责 |
|------|------|------|
| `FocusRecord` | `DatabaseCommon.h` | 64 字节定长记录结构体（`#pragma pack(1)`） |
| `DatabaseManager` | `DatabaseManager.h/.cpp` | `fstream` 随机读写、崩溃恢复 |
| `AbstractPlant` | `AbstractPlant.h` | 植物抽象基类（虚析构函数） |
| `Tree` | `Tree.h/.cpp` | 树类（height_, fruitCount_） |
| `Flower` | `Flower.h/.cpp` | 花类（petalCount_, bloomColor_） |
| `OakTree` | `OakTree.h/.cpp` | 橡树（30/60/100%） |
| `PineTree` | `PineTree.h/.cpp` | 松树（10/55/100%） |
| `Rose` | `Rose.h/.cpp` | 玫瑰 |
| `FocusController` | `FocusController.h/.cpp` | 状态机 + QTimer 计时中枢 |
| `SystemMonitor` | `SystemMonitor.h/.cpp` | WinAPI 前台窗口轮询 |
| `RuleEngine` | `RuleEngine.h/.cpp` | 黑白名单大小写不敏感匹配 |
| `CoinManager` | `CoinManager.h/.cpp` | 金币持久化 |
| `QuoteProvider` | `QuoteProvider.h/.cpp` | 随机自勉语录 |
| `MainWindow` | `MainWindow.h/.cpp` | 主窗口 + closeEvent 拦截 |
| `TimerRing` | `TimerRing.h/.cpp` | 弧形计时圆环（paintEvent） |
| `GardenCanvas` | `GardenCanvas.h/.cpp` | M×N 森林画布 + QPixmap 缓存 |
| `SettingsDialog` | `SettingsDialog.h/.cpp` | 设置对话框 |
| `StoreDialog` | `StoreDialog.h/.cpp` | 商城对话框 |

**共 17 个类，≥5 类 ✓。继承 3 层（AbstractPlant→Tree→OakTree），≥2 层 ✓。**

---

## 三、核心模块实现

### 3.1 定长二进制存储引擎 (DatabaseManager)

**设计要点**：
- `FocusRecord` 使用 `#pragma pack(push, 1)` 消除编译器对齐填充
- `static_assert(sizeof(FocusRecord) == 64)` 编译期校验
- 每条记录固定 64 字节，随机访问偏移量 = `recordId × 64`
- 使用 `std::fstream` 的 `seekg`/`seekp` 实现 O(1) 随机读写与原地更新
- 每次读写前强制 `file_.clear()` 清除 EOF 状态位（防止 fstream 状态锁死）
- `open()` 时若文件不存在则自动创建空文件

**崩溃恢复机制**：
- 开始专注时写入 `status=3`（RUNNING 中间态）
- 正常结束时改写 `status=0/1/2`
- `open()` 时检查最后一条记录：若 `status==3` → 自动标记为枯萎（`status=1, growthStage=4`）
- 入口边界检查：`if (recordCount_ == 0) return;`，防止空库 uint32 下溢

### 3.2 Windows API 系统监听 (SystemMonitor)

**技术要点**：
- **线程模型**：使用 `QTimer` 在主线程 500ms 轮询，完全消除多线程死锁风险
- **权限处理**：`OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, ...)`，**不含** `PROCESS_VM_READ`
  - `PROCESS_VM_READ` 会导致高权限进程的 `OpenProcess` 返回 `ERROR_ACCESS_DENIED`
- **宏污染防护**：`#define NOMINMAX` + `#define WIN32_LEAN_AND_MEAN` 在 `<windows.h>` 之前
- **全链路**：`GetForegroundWindow()` → `GetWindowThreadProcessId()` → `OpenProcess()` → `QueryFullProcessImageNameW()` → 提取文件名 → `RuleEngine::isViolation()`

### 3.3 反作弊规则引擎 (RuleEngine)

- 黑白名单管理（add/remove/set）
- 使用 `QString::compare(name1, name2, Qt::CaseInsensitive)` 实现大小写不敏感匹配
- 进程名全小写/全大写/混合大小写均能正确判定

### 3.4 专注状态机 (FocusController)

```
IDLE ──[startFocus]──► RUNNING ──[tick=0]──► SUCCESS
                         │    ▲
                    [pause]  [resume]
                         ▼    │
                       PAUSED
                         │
               [abandon/triggerViolation]
                         ▼
                       FAILED
```

- 每秒 `tick()` 递减 `remainingSeconds_`、递增 `actualSeconds_`
- 调用 `currentPlant_->grow(actualSeconds_, totalSeconds_)` 驱动多态生长
- 完成/失败时自动通过 `DatabaseManager::updateById()` 持久化记录
- `std::unique_ptr<AbstractPlant>` 管理植物生命周期

---

## 四、技术难点与解决方案

### 4.1 fstream 双向读写 EOF 状态锁死

`fstream` 同时 `in|out` 打开时，读取到 EOF 后 `failbit` 被置起，后续 `write()` 静默失败。解决：每次模式切换前强制 `file_.clear()`。

### 4.2 高权限进程 OpenProcess 失败

申请 `PROCESS_VM_READ` 权限会导致管理员进程拒绝访问。解决：仅使用 `PROCESS_QUERY_LIMITED_INFORMATION`（Vista+ 可用）。

### 4.3 windows.h 宏污染

`<windows.h>` 定义 `min`/`max` 宏覆盖 `std::min`/`std::max`。解决：包含前定义 `NOMINMAX` + `WIN32_LEAN_AND_MEAN`。

### 4.4 虚析构函数缺失

基类指针 `delete` 时不调用子类析构函数 → 内存泄漏。解决：`virtual ~AbstractPlant() = default;`。

### 4.5 数据文件路径硬编码

绝对路径导致跨机器崩溃。解决：使用 `QCoreApplication::applicationDirPath()` 动态获取 exe 目录。

### 4.6 大小写敏感进程名匹配

Windows 进程名不区分大小写。解决：`QString::compare(..., Qt::CaseInsensitive)`。

---

## 五、测试结论

测试覆盖 29 个用例（功能 16 个 + 集成 8 个 + 边界 5 个），全部通过。详细测试报告见 `docs/test_report.md`。

---

## 六、项目总结与反思

本项目从零开始，经历 5 周单人敏捷开发，完成了从需求分析、架构设计到完整实现的软件工程全流程。关键技术收获包括：C++17 `std::unique_ptr` 内存管理、`enum class` 强类型枚举、`#pragma pack` 定长结构体、`fstream` 随机文件 I/O 的 EOF 陷阱处理、Windows API 权限模型（`PROCESS_QUERY_LIMITED_INFORMATION` vs `PROCESS_VM_READ`）、Qt 信号槽松耦合架构、MOC 编译机制、QPainter 自定义控件绘制。

不足与改进方向：精灵图资源未制作（当前使用色块渲染）；白名单模式未实现；白噪音背景音功能未实现；统计图表（饼图/柱状图）未实现。这些可作为后续版本迭代目标。
