# Task 3 report: installer smoke test

## Status

Implemented `scripts/installer_smoketest.ps1`.

The script requires an existing installer and a previously unused `-InstallDir`
below the selected temporary root (`RUNNER_TEMP`, otherwise `TEMP`). It logs
each assertion to `artifacts/installer-smoke.log`, checks the expected runtime
layout and forbidden content, starts and stops only its own application process,
runs the uninstaller, and removes only its validated temporary install path.

`%LOCALAPPDATA%\Forest` is never removed or altered when it already exists. If
it is absent, the script creates a uniquely named sentinel and verifies that it
survives uninstallation; it intentionally leaves that sentinel in place.

## Validation performed

- PowerShell parser validation completed without errors.
- Safe-failure execution used the existing smoke script as `-Installer` and
  `-InstallDir C:\ForestFocus`. It returned exit code `1`, recorded the rejected
  path in the log, and did not log or attempt `RUN: Starting installer`.
- Static checks confirmed one `Remove-Item` call, targeting only `$cleanupPath`
  after its temporary-root validation; no cleanup target references
  `LOCALAPPDATA`, `$HOME`, the workspace, or `$INSTDIR`.
- Static checks also confirmed the required parameters, runtime/forbidden
  assertions, sentinel guard, recursive forbidden-extension scan, three-second
  liveness check, and uninstaller path.
- `git diff --check` completed successfully.

## Limits and concerns

`makensis.exe` is not available on this machine, so no
`release/ForestFocus_Setup.exe` was built and the real install/start/uninstall
smoke run was not attempted. The complete command in the task brief must still
run in an environment that has produced the installer.

The failure log in `artifacts/installer-smoke.log` is expected evidence from the
safe-failure test and is ignored by Git.
