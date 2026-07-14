# Release checklist

Run this checklist from the source and Git root before distributing a release.

## Build and test

1. Configure a clean Release build with `-DCMAKE_PREFIX_PATH="<Qt-root>"`.
2. Build with `cmake --build <build-dir> --config Release --parallel 4`.
3. Run `ctest --test-dir <build-dir> --output-on-failure`.
4. Review the UI smoke screenshots for 1366x768, 1440x900, and 1920x1080. Confirm the focus tag, duration card, start button, and forest header are not clipped.

## Package and smoke test

1. Run `./scripts/package_portable.ps1 -BuildDir <build-dir> -SmokeTest`.
2. Run:

   ```powershell
   ./scripts/package_installer.ps1 -BuildDir <build-dir>
   ./scripts/installer_smoketest.ps1 -Installer ./release/ForestFocus_Setup.exe `
     -InstallDir "$env:TEMP\forest-installer-smoke"
   ```

3. Review `artifacts/*.sha256` and `artifacts/installer-smoke.log`. Confirm user data is not included in the package and is not removed by uninstall.

## Release record

1. Record the build directory, CTest result, screenshot review, package hash, and installer result in `MEMORY.md`.
2. Commit only source, tests, documentation, CI configuration, and scripts. Never commit build directories, runtime data, Qt SDKs, logs, or release artifacts.
