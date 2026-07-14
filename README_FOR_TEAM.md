# forest 小组开发说明

这是用于继续开发的轻量源码包，已排除本地构建产物、Qt SDK、运行数据和便携版成品。

## 包含内容

- `src/`：项目源码、界面代码、图片资源、`resources.qrc`
- `scripts/`：辅助脚本，包含便携版打包脚本
- `project_docs/`：开发计划、报告、测试说明等项目文档
- `CMakeLists.txt`：CMake 构建入口
- `.gitignore`：忽略本地构建产物和运行数据
- `.editorconfig`：统一 UTF-8 BOM + CRLF，避免中文乱码
- `AGENTS.md`：稳定的协作规则；`MEMORY.md`：当前开发记忆；`CLAUDE.md`：补充协作说明

## 不包含内容

- `Qt/`：体积很大，需要自行安装或单独拷贝
- `build/`：本机构建产物，可重新生成
- `artifacts/`：便携包、旧压缩包、日志归档
- `.git/`：Git 历史未包含在此 zip 中
- `*.dat`、`app.log`：本地运行数据

## 开发环境

项目使用 C++17 + Qt 6 + CMake。配置时通过 CMake 的包查找机制定位 Qt；请提供本机 Qt 根目录：

```text
<Qt-root>
```

如果本机没有这个目录，有两种做法：

1. 在命令行设置 `CMAKE_PREFIX_PATH`。
2. 配置时显式传入 `-DCMAKE_PREFIX_PATH="<Qt-root>"`。

## 常用命令

```powershell
cmake -S . -B build-release -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="<Qt-root>"
cmake --build build-release --config Release --parallel 4
ctest --test-dir build-release --output-on-failure
```

生成的可执行文件通常在：

```text
build-release/forest.exe
```

便携版打包脚本：

```powershell
.\scripts\package_portable.ps1 -BuildDir build-release -SmokeTest
```

安装包打包与烟测（从源码根目录运行）：

```powershell
./scripts/package_installer.ps1 -BuildDir build-release
./scripts/installer_smoketest.ps1 -Installer ./release/ForestFocus_Setup.exe `
  -InstallDir "$env:TEMP\forest-installer-smoke"
```

## 注意事项

- 请不要提交或转发 `build/`、`Qt/`、`artifacts/` 等大体积或可再生成目录。
- 源码和文档已统一为 UTF-8 BOM + CRLF；如果编辑器询问编码，请保持 UTF-8 with BOM。
- 如果中文再次显示异常，优先检查编辑器/终端编码，而不是直接改文件内容。
- 发布前按 `project_docs/release_checklist.md` 完成测试、截图审查、包体与哈希检查。
