# Forest Focus

**一款以植物成长和森林收集反馈专注成果的 Windows 桌面应用。**

选择项目标签与植物，开始一段专注；完成后记录真实时长、获得金币，并让自己的森林逐渐生长。游戏化内容作为专注后的反馈，而不是专注过程中的打扰。

> **版本说明**
>
> 当前维护版本为 **1.1.1**，源码位于 [`validation/release-20261010`](https://github.com/shayebushianie/forest_self/tree/validation/release-20261010)。
> 默认分支 `main` 尚未合并这部分业务更新。下面的功能和构建说明以 **1.1.1** 为准，请按文中的命令克隆对应分支。

## 核心功能

| 模块 | 功能 |
| --- | --- |
| 专注计时 | 倒计时与正计时、植物选择、项目标签、可选暂停及深度专注监督 |
| 我的森林 | 用真实专注记录生成森林；查看日、周、月、年统计，筛选并追溯每次记录 |
| 植物商城 | 使用专注获得的金币解锁植物，并在专注时选择已解锁树种 |
| 异色发掘 | 使用金币收集已有植物的异色变体，重复获得时按规则返还金币 |
| 成就与挑战 | 成就奖励、每日签到、每日任务和月度挑战，进度与实际使用记录关联 |
| 时间守护 | 设置每日专注目标，查看完成进度、连续达标天数和历史积累 |
| 项目标签 | 自定义名称与颜色，将专注记录归入不同项目 |
| 系统设置 | 专注与监督规则、外观与无障碍选项、备份恢复和记录导出 |

好友与好友挑战属于**本机账户间的本地功能**，不代表跨设备联网、实时协作或云同步。

## 使用流程

```text
选择标签与植物 → 设置时长 → 开始专注
                            ↓
                   完成或主动放弃
                            ↓
               保存记录并更新森林与统计
                            ↓
                 查看结果、休息或再次专注
```

- 暂停需要先在设置中允许；暂停期间提供五分钟倒计时，到期自动恢复。
- 成功、主动放弃和异常中断会保留不同的记录状态。
- 森林中的植物可以追溯到对应的真实专注记录。
- 商城、成就和挑战不会替代专注主流程。

## 技术栈与结构

- **C++17 + Qt 6 Widgets / QSS**：原生桌面界面。
- **CMake**：构建、测试和来源元数据。
- **SQLite / Qt SQL**：专注历史记录。
- **版本化状态文件**：账户、钱包、标签等小型本地状态。
- **CTest / Qt Test**：存储、迁移、结算、统计与关键界面流程测试。
- **PowerShell / NSIS**：Windows 便携版与安装器构建。

```text
src/
  core/       专注、奖励、统计与其他业务服务
  storage/    SQLite、账户存储、迁移、备份与恢复
  config/     植物注册表与用户偏好
  plant/      植物实现与异色变体
  system/     本地进程监督与规则
  ui/         窗口、页面控制器和复用控件
  tests/      自动化测试
scripts/      测试与打包脚本
cmake/        构建辅助与模块边界检查
project_docs/ 架构决策和项目文档
```

项目采用**单机模块化单体**架构，目前不引入服务端、企业权限、多人同步或复杂分布式基础设施。

## 获取源码

```powershell
git clone --branch validation/release-20261010 https://github.com/shayebushianie/forest_self.git
cd forest_self
```

仓库主要提供源码，不包含本机账户数据、Qt SDK、构建缓存或预编译安装包。安装包未随源码提交，不要把单独的 `forest.exe` 当作完整可运行包。

## 构建与测试

已使用的 Windows 工具链为 **Qt 6.5.3 MinGW 64 位 + MinGW GCC 11.2.0**。请使用与 Qt 包匹配的编译器，避免混用其他 GCC 的运行库。

准备 CMake、Ninja，以及 Qt 的 Widgets、SQL 和 Test 模块。在 PowerShell 中，将 `QT_ROOT_DIR` 和 `MINGW_ROOT` 环境变量分别设为本机 Qt 安装根目录和匹配的 MinGW 根目录，并将 CMake、Ninja 与工具链命令加入 PATH。

```powershell
cmake -S . -B build-release -G Ninja `
  -DCMAKE_BUILD_TYPE=Release `
  "-DCMAKE_PREFIX_PATH=$env:QT_ROOT_DIR" `
  "-DCMAKE_CXX_COMPILER=$env:MINGW_ROOT/bin/g++.exe" `
  -DBUILD_TESTING=ON

cmake --build build-release --parallel 4
ctest --test-dir build-release --output-on-failure
.\build-release\forest.exe
```

仅构建应用时，可在独立构建目录使用 `-DBUILD_TESTING=OFF`，此时不要求 Qt Test。移动源码或更换 Qt / 编译器后，请重新配置构建目录，不要沿用失效缓存。

项目包含存储损坏与迁移、专注终态结算、钱包与奖励一致性、统计边界和 Qt 界面烟雾测试。测试结果应以对应提交的实际运行记录为准。

## 便携版打包

在对应的 Qt 部署工具可用时，可生成包含匹配运行库的便携包：

```powershell
.\scripts\package_portable.ps1 `
  -BuildDir build-release `
  -QtDir $env:QT_ROOT_DIR `
  -SmokeTest
```

输出位于 `artifacts/`。脚本会拒绝覆盖已有产物或带使用数据的目录；再次打包请指定新的 `-OutputDir`。请保留完整运行库和插件目录，不要只复制可执行文件。

## 本地数据与安全边界

- 安装模式使用当前 Windows 用户的本地应用数据目录；只有程序同级存在 `forest.portable` 标记时，才使用同级 `app_data`。
- 不要在已有数据上随意添加或移除便携标记来切换模式。
- 专注记录使用 SQLite；小型状态使用版本、校验、原子保存和备份机制。
- 专注终态及奖励结算具备持久化凭据和重启补偿；无法安全恢复的数据不会静默重置为空。
- 系统设置提供备份、恢复与导出入口。重要数据应定期备份，不能将备份机制视为绝对不丢失的保证。
- 本地登录用于账户数据分离，不是云端身份认证。

## 开发约定

保持现有功能与数据兼容，优先做小范围、可验证的修改。不要提交 `app_data`、账户文件、日志、备份、Qt SDK 或构建产物。

进一步协作说明见 [README_FOR_TEAM.md](https://github.com/shayebushianie/forest_self/blob/validation/release-20261010/README_FOR_TEAM.md)；构建脚本和测试以对应源码分支为准。
