# 可复现安装包发布流水线 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use `superpowers:subagent-driven-development` (recommended) or `superpowers:executing-plans` to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 从源码 Git 根目录构建、验证并归档不包含用户数据的 NSIS 安装包。

**Architecture:** `scripts/package_portable.ps1` 保持唯一的 Qt 运行时收集器；新的安装打包脚本只消费其输出，并把经过拒绝规则验证的内容暂存到 `artifacts/installer-stage/`。NSIS 把暂存内容装入 `$INSTDIR\app`，让卸载器只递归删除自身拥有的子目录；独立烟雾脚本在临时安装目录验证安装、启动、SQLite 驱动与卸载数据保留。

**Tech Stack:** PowerShell 5.1+、NSIS 3、CMake/CTest、GitHub Actions Windows、Qt 6.5.3 MinGW。

## Global Constraints

- 源码 Git 根目录的 `installer/` 是唯一规范安装脚本位置；工作区外层旧 `installer/` 不作为新脚本、CI 或文档入口。
- 安装输入只能来自 `artifacts/forest_portable/`，不得从工作区根目录、Qt SDK 或用户数据目录复制运行时。
- 禁止进入安装暂存或安装目录：`forest.portable`、`app_data/`、`*.sqlite`、`*.dat`、`*.log`、`*.bak` 和用户备份文件。
- 安装模式的用户数据 `%LOCALAPPDATA%/Forest` 永不由打包、烟雾测试或卸载删除、覆盖或迁移。
- 安装包烟雾测试属于发布/CI 流程，不加入 CTest；现有 CTest 数量保持 6。
- 本轮不实现自动发布、代码签名、自动更新或产品功能；未经用户再次明确授权不创建 Git 提交或推送。

---

## 文件结构

- Create: `installer/forest_installer.nsi` — 从受检暂存目录安装到 `$INSTDIR\app` 的 NSIS 定义。
- Create: `scripts/package_installer.ps1` — 构建便携包、生成受检暂存目录、调用 NSIS、输出两种包的 SHA-256。
- Create: `scripts/installer_stage_selftest.ps1` — 以临时伪便携目录验证暂存排除和运行时必需项。
- Create: `scripts/installer_smoketest.ps1` — 安全地安装、启动、卸载并验证用户数据保留。
- Modify: `.github/workflows/ci.yml` — 安装 NSIS，运行安装包构建和烟雾检查，上传新增工件。
- Modify: `.gitignore` — 忽略源码根目录的 `/release/`。
- Modify: `README_FOR_TEAM.md`、`project_docs/release_checklist.md`、`AGENTS.md` — 指向源码根目录安装入口与验证命令。
- Modify: `MEMORY.md` — 在实际验证完成后记录提交号、哈希、CI 结果和仍存风险。

## Task 1: 安装暂存目录的隔离与自测

**Files:**

- Create: `scripts/installer_stage_selftest.ps1`
- Create: `scripts/package_installer.ps1`
- Modify: `.gitignore`

**Interfaces:**

- Consumes: `artifacts/forest_portable/` 中的已部署便携运行时。
- Produces: `scripts/package_installer.ps1 -PortableDir <dir> -StageDir <dir> -StageOnly`；成功时创建仅含可安装运行时的暂存目录。
- Produces: `artifacts/installer-stage/`，其中必须存在 `forest.exe`、`Qt6Sql.dll`、`platforms/qwindows.dll` 和 `sqldrivers/qsqlite.dll`。

- [ ] **Step 1: 编写暂存失败自测脚本**

创建 `scripts/installer_stage_selftest.ps1`。它应在源码 Git 根目录 `artifacts/installer-stage-selftest-<GUID>/` 下创建唯一伪便携输入和暂存目录：

```powershell
$required = @(
    'forest.exe', 'Qt6Sql.dll',
    'platforms\qwindows.dll', 'sqldrivers\qsqlite.dll'
)
$forbidden = @(
    'forest.portable', 'app_data\sessions.sqlite',
    'session.dat', 'app.log', 'backup.bak'
)
foreach ($relative in $required + $forbidden) {
    $path = Join-Path $portableRoot $relative
    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $path) | Out-Null
    New-Item -ItemType File -Force -Path $path | Out-Null
}
& $packageScript -PortableDir $portableRoot -StageDir $stageRoot -StageOnly
if ($LASTEXITCODE -ne 0) { throw 'Stage-only package command failed.' }
foreach ($relative in $required) {
    if (-not (Test-Path -LiteralPath (Join-Path $stageRoot $relative))) {
        throw "Required staged file is missing: $relative"
    }
}
foreach ($relative in $forbidden) {
    if (Test-Path -LiteralPath (Join-Path $stageRoot $relative)) {
        throw "Forbidden staged file exists: $relative"
    }
}
```

Use a `try/finally` block that deletes only the uniquely created `artifacts/installer-stage-selftest-<GUID>/` fixture root after first confirming that its resolved path remains below the repository `artifacts/` directory. The first execution must fail because `package_installer.ps1` does not yet exist.

- [ ] **Step 2: 运行自测确认失败**

Run:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\installer_stage_selftest.ps1
```

Expected: non-zero exit with a missing `package_installer.ps1` or unsupported `-StageOnly` error.

- [ ] **Step 3: 实现受检暂存模式**

创建 `scripts/package_installer.ps1`，至少提供以下参数和阶段函数：

```powershell
param(
    [string]$BuildDir = 'build',
    [string]$PortableDir = 'artifacts/forest_portable',
    [string]$StageDir = 'artifacts/installer-stage',
    [string]$NsisCompiler = '',
    [switch]$NoBuild,
    [switch]$StageOnly
)

$requiredFiles = @(
    'forest.exe', 'Qt6Sql.dll',
    'platforms\qwindows.dll', 'sqldrivers\qsqlite.dll'
)
$forbiddenPattern = '(^|\\)(forest\.portable|app_data)(\\|$)|\.(sqlite|dat|log|bak)$'

function Assert-InstallerStage([string]$stagePath) {
    foreach ($relative in $requiredFiles) {
        if (-not (Test-Path -LiteralPath (Join-Path $stagePath $relative))) {
            throw "Installer stage is missing required runtime: $relative"
        }
    }
    foreach ($file in Get-ChildItem -LiteralPath $stagePath -Recurse -Force -File) {
        $relative = $file.FullName.Substring($stagePath.Length).TrimStart('\')
        if ($relative -match $forbiddenPattern) {
            throw "Installer stage contains forbidden file: $relative"
        }
    }
}

function New-InstallerStage([string]$portablePath, [string]$stagePath) {
    if (-not (Test-Path -LiteralPath $portablePath)) {
        throw "Portable input directory does not exist: $portablePath"
    }
    if (Test-Path -LiteralPath $stagePath) {
        Remove-Item -LiteralPath $stagePath -Recurse -Force
    }
    New-Item -ItemType Directory -Path $stagePath | Out-Null
    foreach ($file in Get-ChildItem -LiteralPath $portablePath -Recurse -Force -File) {
        $relative = $file.FullName.Substring($portablePath.Length).TrimStart('\')
        if ($relative -notmatch $forbiddenPattern) {
            $destination = Join-Path $stagePath $relative
            New-Item -ItemType Directory -Force -Path (Split-Path -Parent $destination) | Out-Null
            Copy-Item -LiteralPath $file.FullName -Destination $destination -Force
        }
    }
    Assert-InstallerStage $stagePath
}
```

Resolve `$repoRoot` from `$PSScriptRoot`; turn `BuildDir`、`PortableDir` 和 `StageDir` into absolute paths below `$repoRoot\artifacts` before invoking `New-InstallerStage`. If `-StageOnly` is present, finish successfully after `Assert-InstallerStage` without locating NSIS or creating an installer. Do not read or remove any path outside the resolved repository `artifacts/` directory.

Add `/release/` to `.gitignore`; leave `/artifacts/` ignored.

- [ ] **Step 4: 运行自测确认通过**

Run:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\installer_stage_selftest.ps1
```

Expected: exit code 0; the fixture contains the four required files in the stage and none of the five forbidden entries.

- [ ] **Step 5: Review gate**

Inspect only the staged diff for `.gitignore`, `scripts/package_installer.ps1` and `scripts/installer_stage_selftest.ps1`. Do not commit without explicit user authorization.

## Task 2: 规范 NSIS 输入与安装布局

**Files:**

- Create: `installer/forest_installer.nsi`
- Modify: `scripts/package_installer.ps1`

**Interfaces:**

- Consumes: a passing `New-InstallerStage` result.
- Produces: `release/ForestFocus_Setup.exe` and `artifacts/ForestFocus_Setup.sha256`.
- Consumes compile-time definitions `/DSTAGE_DIR=<absolute-stage-path>` and `/DOUTPUT_DIR=<absolute-release-path>`.
- Produces install layout `$INSTDIR\app\forest.exe`; shortcuts target this path and uninstaller is `$INSTDIR\Uninstall.exe`.

- [ ] **Step 1: 写入 NSIS 输入契约测试**

Extend `scripts/installer_stage_selftest.ps1` to assert the package script refuses a portable directory missing `sqldrivers\qsqlite.dll`:

```powershell
Remove-Item -LiteralPath (Join-Path $portableRoot 'sqldrivers\qsqlite.dll') -Force
& $packageScript -PortableDir $portableRoot -StageDir $stageRoot -StageOnly 2>$null
if ($LASTEXITCODE -eq 0) {
    throw 'Stage-only command accepted input without qsqlite.dll.'
}
```

Run the selftest before changing NSIS. Expected: it currently fails if the required-runtime assertion was omitted.

- [ ] **Step 2: 创建只消费暂存目录的 NSIS 脚本**

Create `installer/forest_installer.nsi` with the following essential contract:

```nsi
Unicode true
RequestExecutionLevel user
SetCompressor /SOLID lzma

!ifndef STAGE_DIR
!error "STAGE_DIR is required"
!endif
!ifndef OUTPUT_DIR
!error "OUTPUT_DIR is required"
!endif

!include "MUI2.nsh"
!define PRODUCT_NAME "Forest 专注森林"
!define PRODUCT_VERSION "1.1.0"
!define PRODUCT_REGKEY "Software\\ForestFocus"
!define UNINSTALL_REGKEY "Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\ForestFocus"

Name "${PRODUCT_NAME}"
OutFile "${OUTPUT_DIR}\\ForestFocus_Setup.exe"
InstallDir "$LOCALAPPDATA\\Programs\\ForestFocus"
InstallDirRegKey HKCU "${PRODUCT_REGKEY}" "InstallDir"

Section "安装主程序" SecMain
    SetOutPath "$INSTDIR\\app"
    File /r "${STAGE_DIR}\\*.*"
    WriteUninstaller "$INSTDIR\\Uninstall.exe"
    WriteRegStr HKCU "${PRODUCT_REGKEY}" "InstallDir" "$INSTDIR"
    WriteRegStr HKCU "${UNINSTALL_REGKEY}" "UninstallString" '"$INSTDIR\\Uninstall.exe"'
    SetShellVarContext current
    CreateShortCut "$SMPROGRAMS\\${PRODUCT_NAME}\\${PRODUCT_NAME}.lnk" "$INSTDIR\\app\\forest.exe"
SectionEnd

Section "Uninstall"
    SetShellVarContext current
    Delete "$SMPROGRAMS\\${PRODUCT_NAME}\\${PRODUCT_NAME}.lnk"
    RMDir "$SMPROGRAMS\\${PRODUCT_NAME}"
    Delete "$INSTDIR\\Uninstall.exe"
    RMDir /r "$INSTDIR\\app"
    DeleteRegKey HKCU "${UNINSTALL_REGKEY}"
    DeleteRegKey HKCU "${PRODUCT_REGKEY}"
    RMDir "$INSTDIR"
SectionEnd
```

Add the standard MUI welcome, directory, install-files, finish, uninstall-confirm and uninstall-files pages. Keep all application payload below `$INSTDIR\app`; never use `RMDir /r "$INSTDIR"`.

- [ ] **Step 3: 完成打包器的 NSIS 解析、构建和哈希**

Extend `scripts/package_installer.ps1` after `New-InstallerStage`:

```powershell
function Resolve-NsisCompiler {
    $candidates = @(
        $NsisCompiler,
        $env:NSIS_COMPILER,
        (Join-Path ${env:ProgramFiles(x86)} 'NSIS\makensis.exe'),
        (Join-Path $env:ProgramFiles 'NSIS\makensis.exe')
    ) | Where-Object { $_ } | Select-Object -Unique
    foreach ($candidate in $candidates) {
        if (Test-Path -LiteralPath $candidate) { return (Resolve-Path -LiteralPath $candidate).Path }
    }
    throw 'makensis.exe not found. Pass -NsisCompiler, set NSIS_COMPILER, or install NSIS.'
}

$releaseDir = Join-Path $repoRoot 'release'
New-Item -ItemType Directory -Force -Path $releaseDir | Out-Null
$nsis = Resolve-NsisCompiler
$script = Join-Path $repoRoot 'installer\forest_installer.nsi'
& $nsis "/DSTAGE_DIR=$stagePath" "/DOUTPUT_DIR=$releaseDir" $script
if ($LASTEXITCODE -ne 0) { throw "NSIS build failed with exit code $LASTEXITCODE." }
$installer = Join-Path $releaseDir 'ForestFocus_Setup.exe'
if (-not (Test-Path -LiteralPath $installer)) { throw "NSIS did not create $installer" }
Get-FileHash -LiteralPath $installer -Algorithm SHA256 |
    ForEach-Object { "{0} *ForestFocus_Setup.exe" -f $_.Hash } |
    Set-Content -LiteralPath (Join-Path $repoRoot 'artifacts\ForestFocus_Setup.sha256') -Encoding ascii
```

Before `New-InstallerStage`, call `scripts/package_portable.ps1 -BuildDir $BuildDir`; pass `-NoBuild` when the installer script receives `-NoBuild`. After that command succeeds, write `artifacts/forest_portable.sha256` from `artifacts/forest_portable.zip` using the same one-line `HASH *filename` format.

- [ ] **Step 4: 验证真实安装包构建**

Run:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\package_installer.ps1 -BuildDir build-architecture-upgrade
Get-FileHash .\release\ForestFocus_Setup.exe -Algorithm SHA256
Get-Content .\artifacts\ForestFocus_Setup.sha256
```

Expected: installer exists; both commands print the same SHA-256; the package command has already rejected any missing Qt SQL runtime before NSIS runs.

- [ ] **Step 5: Review gate**

Inspect the NSIS script for exactly one recursive delete target: `$INSTDIR\app`. Do not commit without explicit user authorization.

## Task 3: 安全的安装、启动与卸载烟雾测试

**Files:**

- Create: `scripts/installer_smoketest.ps1`

**Interfaces:**

- Consumes: `release/ForestFocus_Setup.exe` and an explicitly supplied temporary `-InstallDir`.
- Produces: `artifacts/installer-smoke.log`; exit code 0 only after all install, startup, forbidden-content and uninstall assertions pass.
- Requires: `-InstallDir` resolves inside `%TEMP%` or `$env:RUNNER_TEMP`; it must not equal an existing production installation directory.

- [ ] **Step 1: 写入安全失败用例**

Create a minimal command in `scripts/installer_smoketest.ps1` that rejects an install path outside the temporary root:

```powershell
$tempRoot = [IO.Path]::GetFullPath(
    $(if ($env:RUNNER_TEMP) { $env:RUNNER_TEMP } else { [IO.Path]::GetTempPath() })
).TrimEnd('\') + '\'
$installPath = [IO.Path]::GetFullPath($InstallDir)
if (-not $installPath.StartsWith($tempRoot, [StringComparison]::OrdinalIgnoreCase)) {
    throw "InstallDir must be below the temporary root: $tempRoot"
}
```

Run with `-InstallDir C:\ForestFocus` and expect a non-zero exit before any installer is started.

- [ ] **Step 2: 实现安装包烟雾测试**

Use this parameter surface and checks:

```powershell
param(
    [Parameter(Mandatory = $true)][string]$Installer,
    [Parameter(Mandatory = $true)][string]$InstallDir,
    [string]$LogPath = 'artifacts/installer-smoke.log'
)

$expected = @(
    'app\forest.exe', 'app\Qt6Sql.dll',
    'app\platforms\qwindows.dll', 'app\sqldrivers\qsqlite.dll'
)
$forbidden = @('app\forest.portable', 'app\app_data')
```

The script must:

1. Resolve `$Installer`, `$InstallDir`, `$LogPath` to absolute paths; require the installer exists, require no `forest` process is already running, and require the installation directory is below the temporary root.
2. Before starting the installer, inspect `%LOCALAPPDATA%\Forest`. If it does not exist, create that directory and a unique `installer-smoke-sentinel.txt`; record `$sentinelCreated = $true`. If it exists, record that preservation verification is skipped and never modify it.
3. Invoke `Start-Process -FilePath $Installer -ArgumentList '/S', "/D=$installPath" -Wait -PassThru`; require exit code 0. Reject whitespace in `$installPath` before this call because NSIS `/D=` accepts an unquoted final path.
4. Assert every `$expected` path exists, every `$forbidden` path is absent, and a recursive file scan finds no extension matching `\.sqlite$|\.dat$|\.log$|\.bak$` below `$installPath\app`.
5. Start `$installPath\app\forest.exe` hidden, wait three seconds, require the process is still live, then stop only the process object created by this script.
6. Run `$installPath\Uninstall.exe /S`, require exit code 0, then require `app\forest.exe` is absent. If `$sentinelCreated`, require the sentinel still exists; never remove this sentinel or any other user-data file.
7. Write every assertion and final status to `$LogPath`. In `finally`, remove only `$installPath` after proving its resolved path remains under the temporary root; leave LocalAppData untouched.

- [ ] **Step 3: 验证安装、启动、卸载和数据保留**

Run after Task 2:

```powershell
$dir = Join-Path $env:TEMP "forest-installer-smoke-$PID"
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\installer_smoketest.ps1 `
  -Installer .\release\ForestFocus_Setup.exe -InstallDir $dir
Get-Content .\artifacts\installer-smoke.log
```

Expected: exit code 0; log reports all required runtime paths, live process check, successful uninstallation and either preserved sentinel or safely skipped existing-data check.

- [ ] **Step 4: Review gate**

Verify the smoke script has no `Remove-Item` call targeting `%LOCALAPPDATA%`, `$HOME`, the workspace root or `$INSTDIR` outside its computed temporary path. Do not commit without explicit user authorization.

## Task 4: CI、协作文档与发布验证

**Files:**

- Modify: `.github/workflows/ci.yml`
- Modify: `README_FOR_TEAM.md`
- Modify: `project_docs/release_checklist.md`
- Modify: `AGENTS.md`
- Modify: `MEMORY.md`

**Interfaces:**

- Consumes: Task 1–3 scripts and source-root `installer/forest_installer.nsi`.
- Produces: CI artifact set containing portable ZIP, installer EXE, both hash files, installer log, CTest log and UI screenshots.

- [ ] **Step 1: 更新 Windows CI**

Insert these steps after the existing portable package smoke step and before artifact upload:

```yaml
      - name: Install NSIS
        shell: pwsh
        run: choco install nsis --yes --no-progress
      - name: Package installer
        shell: pwsh
        run: >-
          ./scripts/package_installer.ps1 -BuildDir build
          -NsisCompiler "${env:ProgramFiles(x86)}\NSIS\makensis.exe" -NoBuild
      - name: Installer smoke test
        shell: pwsh
        run: >-
          ./scripts/installer_smoketest.ps1
          -Installer ./release/ForestFocus_Setup.exe
          -InstallDir "${env:RUNNER_TEMP}\forest-installer-smoke"
```

Extend `actions/upload-artifact` paths with:

```yaml
            release/ForestFocus_Setup.exe
            artifacts/*.sha256
            artifacts/installer-smoke.log
```

Keep `if: always()` so logs remain available after a failure.

- [ ] **Step 2: 更新协作与发布文档**

In `README_FOR_TEAM.md`, add the source-root commands:

```powershell
./scripts/package_installer.ps1 -BuildDir build-release
./scripts/installer_smoketest.ps1 -Installer ./release/ForestFocus_Setup.exe `
  -InstallDir "$env:TEMP\forest-installer-smoke"
```

In `project_docs/release_checklist.md`, replace the workspace-level installer note with the two commands above and require review of `artifacts/*.sha256` plus `artifacts/installer-smoke.log`.

In `AGENTS.md`, change the installer rule to state that the source-root `installer/forest_installer.nsi`, `scripts/package_installer.ps1` and `scripts/installer_smoketest.ps1` are the canonical release entry; retain the rule that user data is never packaged or deleted.

After actual verification, update `MEMORY.md` with the exact CTest result, both SHA-256 values, installer smoke outcome, whether user-data sentinel verification ran, and the first remote CI run URL/result. Do not claim a remote CI result before it exists.

- [ ] **Step 3: 本地发布验证**

Run exactly:

```powershell
cmake --build build-architecture-upgrade --config Release --parallel 4
ctest --test-dir build-architecture-upgrade -C Release --output-on-failure
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\installer_stage_selftest.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\package_installer.ps1 -BuildDir build-architecture-upgrade -NoBuild
$dir = Join-Path $env:TEMP "forest-installer-smoke-$PID"
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\installer_smoketest.ps1 `
  -Installer .\release\ForestFocus_Setup.exe -InstallDir $dir
```

Expected: six CTest tests pass; stage selftest, installer build and installer smoke all return 0; `artifacts/forest_portable.sha256`, `artifacts/ForestFocus_Setup.sha256` and `artifacts/installer-smoke.log` exist; no new runtime data is staged or committed.

- [ ] **Step 4: 远程 CI 验收**

Trigger the normal CI only after a user-authorized source commit exists. Review the downloadable artifact for all six UI screenshots, `LastTest.log`, the portable ZIP, `ForestFocus_Setup.exe`, both `.sha256` files and `installer-smoke.log`. Record only the observed workflow URL, result and artifact review outcome in `MEMORY.md`.

- [ ] **Step 5: Final review gate**

Run:

```powershell
git status --short
git check-ignore -v artifacts\forest_portable.zip release\ForestFocus_Setup.exe
```

Expected: generated ZIP and installer are ignored; no user data, build directory, Qt SDK, log or release artifact is staged. Request explicit user authorization before committing; do not push.

## Plan Self-Review

- Spec coverage: Task 1 enforces sole portable input and data exclusion; Task 2 provides source-root NSIS and SQLite runtime coverage; Task 3 handles installation, startup, safe uninstallation and sentinel retention; Task 4 provides CI artifacts, documentation and release evidence.
- Scope: no task adds remote publishing, signing, auto-update or product features.
- Safety: every filesystem removal is constrained to a repository `artifacts/` directory or an explicitly resolved temporary installation directory; no task permits LocalAppData cleanup.
- Interface consistency: all tasks use `package_installer.ps1 -BuildDir/-PortableDir/-StageDir/-NoBuild/-StageOnly`, source-root `installer/forest_installer.nsi`, and `installer_smoketest.ps1 -Installer/-InstallDir/-LogPath`.
