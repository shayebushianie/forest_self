# Release checklist

Run this checklist from the source and Git root before distributing a release.

## Build and test

1. Configure a clean Release build with `-DCMAKE_PREFIX_PATH="<Qt-root>"`.
2. Build with `cmake --build <build-dir> --config Release --parallel 4`.
3. Run `ctest --test-dir <build-dir> --output-on-failure`.
4. Review the UI smoke screenshots for 1366x768, 1440x900, and 1920x1080. Confirm the focus tag, duration card, start button, and forest header are not clipped.

## Package and smoke test

1. Run `./scripts/package_portable.ps1 -BuildDir <build-dir> -SmokeTest`.
2. Record the SHA-256 of the portable archive and the deployed executable.
3. When producing the NSIS installer from the workspace-level `installer/` directory, install it into a temporary directory, launch it, then uninstall it. Confirm user data is not included in the package and is not removed by uninstall.

## Release record

1. Record the build directory, CTest result, screenshot review, package hash, and installer result in `MEMORY.md`.
2. Commit only source, tests, documentation, CI configuration, and scripts. Never commit build directories, runtime data, Qt SDKs, logs, or release artifacts.
