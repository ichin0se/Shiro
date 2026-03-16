# Shiro

Shiro is a production-oriented physical renderer skeleton targeting USD Hydra Render Delegate integration and a staged XPU execution model. The current codebase establishes the module boundaries for:

- frontend/Hydra plugin entry points and scene translation.
- runtime/framebuffer/image IO infrastructure.
- CPU path tracing with a physically based material core.
- a CUDA/OptiX backend for simple triangle scenes.
- OpenQMC-backed sampler integration with a deterministic fallback.
- AOV buffers and render-pass orchestration.

The implementation in this repository is still the early stage of a larger renderer. It is intentionally structured so that BVH, wavefront GPU kernels, MaterialX/OpenPBR translation, spectral transport, and advanced light transport techniques can be added without breaking the public boundaries between frontend, runtime, and backend modules.

For persistent session context, see `docs/codex-handoff.md`.

## Current build status

Validated configurations:

- `Houdini 20.5.410` on AlmaLinux 9 using SideFX USD 24.03 (`PXR_VERSION 2403`)
- `OpenUSD` via `vcpkg` on Windows for the standalone development path
- bundled `OpenQMC`

Validated Linux build commands:

```bash
cmake --fresh --preset linux-houdini20.5
cmake --build --preset linux-houdini20.5
cmake --install build-linux-houdini20.5
```

Validated Windows build commands:

```powershell
& "C:\Program Files\CMake\bin\cmake.exe" --fresh --preset vs2022-x64
& "C:\Program Files\CMake\bin\cmake.exe" --build --preset vs2022-debug
& "C:\Program Files\CMake\bin\cmake.exe" --build --preset vs2022-release
```

Successful Linux outputs include:

- `build-linux-houdini20.5/hdShiro.so`
- `build-linux-houdini20.5/stage/dso/usd/hdShiro.so`
- `build-linux-houdini20.5/stage/dso/usd_plugins/hdShiro/resources/plugInfo.json`
- `build-linux-houdini20.5/stage/UsdRenderers.json`
- `build-linux-houdini20.5-cuda/tools/shiro_optix_probe`

Successful Windows outputs include:

- `build-vs2022/Debug/shiro_core.lib`
- `build-vs2022/Debug/hdShiro.dll`
- `build-vs2022/Release/shiro_core.lib`
- `build-vs2022/Release/hdShiro.dll`

## Dependency policy

Shiro's intended architecture is DCC-independent.

- Hydra is the integration boundary, not the dependency root.
- Image/texture IO should come from Shiro's own dependencies, such as standalone `OpenImageIO`.
- Houdini-bundled libraries are acceptable only as development fallback while bringing up the delegate inside Solaris.

The current build therefore prefers standalone `OpenImageIO` and only falls back to Houdini's bundled OIIO when `SHIRO_ALLOW_HOUDINI_OIIO_FALLBACK=ON`.
If you want configure-time enforcement, set `SHIRO_REQUIRE_OIIO=ON`.

CUDA/OptiX are also optional dependencies:

- `SHIRO_ENABLE_CUDA` controls CUDA toolkit integration.
- `SHIRO_ENABLE_OPTIX` controls the OptiX backend.
- `SHIRO_REQUIRE_CUDA=ON` and `SHIRO_REQUIRE_OPTIX=ON` turn those into configure-time requirements.
- the current OptiX backend supports triangle GAS traversal, directional and dome-light HDRI sampling, `UsdPreviewSurface` / `standard_surface` / `openpbr_surface`-style PBR shading, and beauty/albedo/normal/depth AOV output.
- the repository vendors OptiX 9.0 headers under `external/optix-sdk-9.0.0` because the local driver stack is `580.76.05`; that combination is runtime-compatible, while OptiX 9.1 headers are not.

## Houdini / usdview usage

Use the staged package directly:

```bash
export HOUDINI_PATH="$PWD/build-linux-houdini20.5/stage;&"
export PXR_PLUGINPATH_NAME="$PWD/build-linux-houdini20.5/stage/dso/usd_plugins"
```

For a quick scene, use `testscenes/minimal.usda`.
