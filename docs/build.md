# Build Notes

## Installed toolchain

Validated on this machine:

- Visual Studio Community 2022 17.14.28
- MSVC 19.44.35224
- Windows SDK 10.0.26100.0
- CMake 4.2.3
- Git 2.53.0.windows.2

## Configure

```powershell
& "C:\Program Files\CMake\bin\cmake.exe" --fresh --preset vs2022-x64
```

## Build

```powershell
& "C:\Program Files\CMake\bin\cmake.exe" --build --preset vs2022-debug
& "C:\Program Files\CMake\bin\cmake.exe" --build --preset vs2022-release
```

## Current status

- Full manifest configure succeeds with `vcpkg` + `pxr`.
- `shiro_core` and `hdShiro` both build successfully in `Debug` and `Release`.
- OpenQMC is wired through the bundled source tree and falls back deterministically if it is disabled.

## Notes

- `vcpkg` installs OpenUSD into the build-local manifest directory during `cmake --fresh --preset vs2022-x64`.
- `hdShiro` copies `plugins/hdShiro/plugInfo.json` into the target output's `resources` directory after build.
- MSVC still reports one warning from the vendored OpenQMC headers in `OpenQmcSampler.cpp`; this is upstream code, not a failure in Shiro itself.
