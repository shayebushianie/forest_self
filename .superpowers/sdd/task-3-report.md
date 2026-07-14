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

## Review correction (ancestor reparse escape)

The reparse guard now walks every existing lexical component from the install
directory back to the selected temporary root, rejecting any reparse point on
that chain. It also resolves the existing install path and temporary root and
requires the canonical install target to remain below the canonical temporary
root. This closes the `%TEMP%\pivot\install` case where `pivot` is a junction
outside the selected temporary root.

Added `scripts/installer_smoketest_ancestor_selftest.ps1`. It selects an
isolated `RUNNER_TEMP`, creates `RUNNER_TEMP\pivot` as a junction to a sibling
external fixture, and has a fake installer create `pivot\install`. The test
requires a `FINAL: FAIL` reparse rejection, verifies the external sentinel
survives, and confirms the pivot junction was not removed. It skips cleanly if
junctions are unavailable.

Fresh validation after this correction:

- Parser checks passed for the smoke script and both junction fixtures.
- The external `C:\ForestFocus` rejection still exits non-zero before starting
  an installer.
- Both descendant- and ancestor-junction fixtures passed, preserving their
  external sentinels and recording cleanup refusal in `FINAL: FAIL`.
- Static checks confirmed non-recursive deletion, descendant and ancestor
  reparse guards, canonical temporary-root containment, and no LocalAppData
  cleanup target.
- `git diff --check` completed successfully.

## Limits and concerns

`makensis.exe` is not available on this machine, so no
`release/ForestFocus_Setup.exe` was built and the real install/start/uninstall
smoke run was not attempted. The complete command in the task brief must still
run in an environment that has produced the installer.

The failure log in `artifacts/installer-smoke.log` is expected evidence from the
safe-failure test and is ignored by Git.

## Review correction (reparse-point cleanup)

The cleanup path is now protected by more than a lexical temporary-root check.
Immediately before filesystem cleanup, the smoke script rejects an `InstallDir`
that is itself a reparse point or contains any reparse-point descendant. Its
cleanup walks ordinary directories explicitly and never uses recursive
`Remove-Item`, so it does not traverse a junction that appears in the install
tree.

Exception handling now records the primary smoke failure and any cleanup or
`Stop-Process` failure, then writes a combined `FINAL: FAIL` record after the
`finally` block and before rethrowing.

Added `scripts/installer_smoketest_selftest.ps1`. Where junctions are supported,
it runs a fake installer that creates an `InstallDir\escape` junction to an
external temporary fixture. The test confirms the smoke script refuses cleanup,
records that refusal in `FINAL: FAIL`, leaves the external sentinel intact, and
leaves the junction-bearing install directory for the fixture's own safe
teardown. It prints `SKIP` and exits successfully if junction fixtures are not
supported.

Fresh validation after the correction:

- Both PowerShell scripts parse without errors.
- The external `C:\ForestFocus` rejection still returns non-zero before an
  installer starts.
- The junction fixture passed on this machine and logged the reparse-point
  cleanup refusal as `FINAL: FAIL`.
- Static checks confirmed no recursive `Remove-Item`, no protected user-data
  cleanup target, and the reparse/combined-final-failure guards.
- `git diff --check` completed successfully.
