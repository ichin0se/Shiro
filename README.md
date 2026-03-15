# Shiro

Shiro is a production-oriented physical renderer skeleton targeting USD Hydra Render Delegate integration and a future XPU execution model. The current codebase establishes the module boundaries for:

- Hydra plugin entry points and scene translation.
- CPU path tracing with a physically based material core.
- OpenQMC-backed sampler integration with a deterministic fallback.
- AOV buffers and render-pass orchestration.

The implementation in this repository is the first stage of a larger renderer. It is intentionally structured so that BVH, wavefront GPU kernels, MaterialX/OpenPBR translation, spectral transport, and advanced light transport techniques can be added without breaking the public boundaries between modules.

## Current build status

The repository is configured in full-manifest mode with:

- `OpenUSD 26.3` via `vcpkg`
- bundled `OpenQMC`
- Visual Studio 2022 + MSVC on Windows

Validated build commands:

```powershell
& "C:\Program Files\CMake\bin\cmake.exe" --fresh --preset vs2022-x64
& "C:\Program Files\CMake\bin\cmake.exe" --build --preset vs2022-debug
& "C:\Program Files\CMake\bin\cmake.exe" --build --preset vs2022-release
```

Successful outputs include:

- `build-vs2022/Debug/shiro_core.lib`
- `build-vs2022/Debug/hdShiro.dll`
- `build-vs2022/Release/shiro_core.lib`
- `build-vs2022/Release/hdShiro.dll`
