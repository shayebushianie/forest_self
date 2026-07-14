# 可复现安装包发布流水线设计

状态：已获产品方向批准，待实施计划评审。

## 目标与范围

从源码 Git 根目录在干净 Windows 环境中构建、验证并归档 NSIS 安装包。安装包以已验证的便携发布目录为唯一运行时输入，安装与卸载不得打包、删除或覆盖用户数据。

本设计不包含自动发布、远端推送、代码签名、自动更新或产品功能改动。

## 规范目录与输入

- 源码 Git 根目录的 `installer/` 是安装包脚本的唯一规范位置。工作区外层旧 `installer/` 在实施期间保留，不作为新 CI 或文档入口。
- `scripts/package_installer.ps1` 接收 `-BuildDir` 和可选 `-NsisCompiler`；它先调用现有 `scripts/package_portable.ps1` 生成已部署且已启动验证的便携目录。
- 安装打包前复制到 `artifacts/installer-stage/`。暂存必须拒绝 `forest.portable`、`app_data/`、`*.sqlite`、`*.dat`、`*.log`、`*.bak` 与用户备份文件。
- 仅暂存目录可作为 NSIS `File` 指令输入，禁止从工作区根目录或用户数据目录读取 DLL、插件或资源。

## 安装与卸载契约

- NSIS 递归安装暂存目录的运行时，使 `Qt6Sql.dll`、`sqldrivers/qsqlite.dll` 及所有便携包需要的 Qt 插件随安装包进入 `$INSTDIR`。
- NSIS 输出目录、版本和暂存目录通过编译期定义传入；脚本不包含机器绝对路径。
- 构建脚本只通过 `-NsisCompiler`、环境变量或标准 NSIS 安装位置定位 `makensis.exe`；找不到时失败并给出定位方式。
- 卸载仅删除安装产生的程序文件、快捷方式和注册表项。`%LOCALAPPDATA%/Forest`、备份、数据库和用户选择的外部文件永不作为卸载目标。

## 安装包烟雾测试

新增独立 PowerShell 脚本，输入安装程序与受控临时安装目录，按顺序执行：

1. 静默安装，确认 `forest.exe`、`Qt6Sql.dll`、`platforms/qwindows.dll` 和 `sqldrivers/qsqlite.dll` 存在。
2. 隐藏启动安装版三秒并确认进程未提前退出，再停止该进程。
3. 如果 `%LOCALAPPDATA%/Forest` 在测试前不存在，创建测试哨兵文件；静默卸载后断言哨兵仍存在。若该目录已有真实内容，脚本不写入、不删除，并报告该断言已安全跳过。
4. 确认安装目录不含 `forest.portable`、`app_data`、数据库或日志；删除仅由测试创建的临时安装目录。

安装包烟雾测试属于发布/CI 阶段，而非 CTest，因为它依赖外部 NSIS 与系统安装行为。

## CI 与工件

Windows CI 在现有构建、6 项 CTest 和便携包烟雾之后安装 NSIS，并执行安装包构建和安装/卸载烟雾测试。无论结果如何，上传：

- UI 三分辨率截图与 CTest 日志；
- 便携 ZIP 与 SHA-256；
- 安装程序、SHA-256 与安装烟雾日志。

发布检查清单和 `MEMORY.md` 记录实际构建目录、测试结果、截图审查、两种包的哈希及安装验证结果。

## 验收标准

- 只检出源码 Git 仓库的 Windows CI 可构建两种发布包。
- 安装版可启动且 SQLite Qt 驱动可用。
- 安装包和安装目录不包含运行时用户数据或便携标记。
- 卸载后用户数据目录保留，且 CI 有可下载的日志与哈希记录。
