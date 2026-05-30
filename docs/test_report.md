# Forest 专注森林 — 测试报告

## 一、测试环境

| 项目 | 详情 |
|------|------|
| 操作系统 | Windows 10/11 x64 |
| 编译器 | MSVC 2019/2022 |
| Qt 版本 | 6.5.3 |
| C++ 标准 | C++17 |
| 测试方式 | 控制台自动化测试 + 手动功能验证 |

## 二、功能测试用例

### 测试组 1：数据库存储引擎（Week 1）

| 编号 | 测试项 | 输入 | 预期结果 | 状态 |
|:----:|--------|------|----------|:----:|
| 1.1 | 文件创建 | 调用 `open()`，文件不存在 | 自动创建 `sessions.dat`，`count()=0` | PASS |
| 1.2 | 追加写入 | 连续 `append()` 5 条记录 | `count()=5`，每条 `recordId` 按序递增 | PASS |
| 1.3 | 随机读取 | `readById(2)` | 返回 `plantType=2`, `plannedMinutes=35` | PASS |
| 1.4 | 原地更新 | `updateById(2, modified)` 将 status 改为 0 | 再读取，`status=0`, `growthStage=3`, `coinsEarned=7` | PASS |
| 1.5 | 越界读取 | `readById(99)` | 返回 `nullopt` | PASS |
| 1.6 | 越界更新 | `updateById(99, dummy)` | 返回 `false` | PASS |
| 1.7 | 全量查询 | `getAllRecords()` | 返回 5 条记录 | PASS |
| 1.8 | 持久化验证 | `close()` → `open()` → `readById(2)` | 数据完整，`coinsEarned=7` | PASS |
| 1.9 | 崩溃恢复 | `status=3` → `close()` → `open()` | 自动标记 `status=1`, `growthStage=4` | PASS |

### 测试组 2：OOP 继承体系与状态机（Week 2）

| 编号 | 测试项 | 输入 | 预期结果 | 状态 |
|:----:|--------|------|----------|:----:|
| 2.1 | OakTree 生长 | `startFocus(0, 5)`, 模拟 300 tick | 30%→Sprout, 60%→Sapling, 100%→Mature, `state=SUCCESS` | PASS |
| 2.2 | PineTree 快速发芽 | `startFocus(1, 5)`, 模拟 300 tick | 10%→Sprout, 55%→Sapling, 100%→Mature | PASS |
| 2.3 | Rose 枯萎 | `startFocus(2, 5)`, 模拟 150 tick → `abandonFocus()` | `state=FAILED`, DB `growthStage=4`, `petalCount_=0` | PASS |
| 2.4 | 暂停/恢复 | 60 tick → `pauseFocus()` → 10 tick → `resumeFocus()` → 完成 | pause 期间 tick 被忽略，`actualSeconds` 不变 | PASS |
| 2.5 | 金币计算 | 5 分钟专注完成 | `coinsEarned=1`（5/5=1） | PASS |

### 测试组 3：Windows API 系统监听（Week 3）

| 编号 | 测试项 | 输入 | 预期结果 | 状态 |
|:----:|--------|------|----------|:----:|
| 3.1 | 大小写不敏感匹配 | `isViolation("NOTEPAD.EXE")` | 黑名单含 `notepad.exe` → 返回 `true` | PASS |
| 3.2 | 非黑名单进程 | `isViolation("devenv.exe")` | 返回 `false` | PASS |
| 3.3 | 空进程名 | `isViolation("")` | 返回 `false` | PASS |
| 3.4 | 黑名单增删 | `addToBlacklist("test.exe")` → `isViolation("test.exe")` → `removeFromBlacklist` | 增删正确 | PASS |
| 3.5 | 违规触发 | 专注中 `triggerViolation("msedge.exe")` | `state=FAILED`, DB `status=1`, `growthStage=4`, `coinsEarned=0` | PASS |
| 3.6 | 实时窗口监听 | 开启监控 → 手动打开 Chrome | 弹窗警告，专注失败，小树枯萎 | PASS |
| 3.7 | 语录系统 | `getRandomQuote()` | 返回非空字符串，共 16 条 | PASS |
| 3.8 | 语录重复 | 连续调用 100 次 | 分布在 16 条中 | PASS |

### 测试组 4：UI 界面与交互（Week 4）

| 编号 | 测试项 | 输入 | 预期结果 | 状态 |
|:----:|--------|------|----------|:----:|
| 4.1 | 计时圆环 | 25 分钟专注 | 弧形进度从满到空，颜色绿→黄→红渐变 | PASS |
| 4.2 | 森林画布 | 历史数据加载 | M×N 网格渲染，成功=绿色方块，失败=灰色方块 | PASS |
| 4.3 | 关闭窗口拦截 | 专注中点击 X | "退出会导致小树枯萎" 弹窗，可选确认/取消 | PASS |
| 4.4 | 设置对话框 | 打开设置 | 植物选择/时长/标签/黑名单控件完整 | PASS |
| 4.5 | 黑名单管理 | 添加/移除进程名 | 列表实时更新，RuleEngine 同步 | PASS |
| 4.6 | 商城对话框 | 打开商城 | 显示金币余额 + 可解锁物品列表 | PASS |
| 4.7 | 暂停/继续按钮 | 专注中点击暂停 | 按钮变"继续"，计时停止，监控停止 | PASS |
| 4.8 | 系统托盘 | 右键托盘图标 | 显示菜单（显示/退出），气泡通知 | PASS |

## 三、边界与健壮性测试

| 编号 | 测试项 | 输入 | 预期结果 | 状态 |
|:----:|--------|------|----------|:----:|
| 5.1 | 空数据库崩溃恢复 | 首次运行，`count()=0` | `recoverFromCrash()` 立即返回，无下溢 | PASS |
| 5.2 | 极端时长-最小 | `startFocus(0, 10)` 10 分钟 | 正常完成，`coinsEarned=2` | PASS |
| 5.3 | 极端时长-最大 | `startFocus(0, 120)` 120 分钟 | 正常完成，`coinsEarned=24` | PASS |
| 5.4 | 空黑名单 | 黑名单为空时专注 | 永不触发违规 | PASS |
| 5.5 | 超长进程名 | 系统返回 260 字符进程路径 | `QueryFullProcessImageNameW` 正常截断 | PASS |

## 四、测试总结

| 指标 | 数值 |
|------|:----:|
| 功能测试用例总数 | 29 |
| 全通过 | 29 |
| 失败 | 0 |
| 通过率 | 100% |
