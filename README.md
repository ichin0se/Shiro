# Shiro

Shiro is an experimental physical renderer built around a USD Hydra render delegate.
The repository is structured to support a staged XPU renderer architecture:

- a Hydra-facing frontend for scene ingestion and render-pass orchestration
- backend-independent runtime types for frame buffers, sampling, and scene data
- a CPU reference path tracer
- a CUDA/OptiX GPU backend
- translation from Hydra material/light state into a renderer-owned scene snapshot

The project is not feature-complete yet. The codebase is already usable for renderer bring-up, backend experiments, Hydra integration work, and early image-quality validation, but it should still be treated as an active renderer prototype rather than a production-finished renderer.

For persistent session context and recent implementation notes, see `docs/codex-handoff.md`.

## What Shiro Currently Covers

The current repository includes:

- Hydra plugin packaging and registration for single-library `Shiro CPU`, `Shiro XPU`, and `Shiro GPU` renderer variants
- render settings under the `shiro:*` namespace
- CPU path tracing backed by Embree
- CUDA/OptiX traversal for triangle scenes
- beauty, albedo, normal, and depth AOV output
- directional lights and dome lights, including HDRI-based dome sampling
- translation for `UsdPreviewSurface`, `standard_surface`, and `openpbr_surface`-style authored parameters
- deterministic sampling with OpenQMC integration plus a fallback sampler path
- small validation tools and USD scenes for renderer bring-up

## What Is Still In Progress

Shiro is intentionally ahead in architecture compared to feature completeness.
Known gaps at the time of writing include:

- no true hybrid/XPU scheduler yet
- limited texture-network evaluation compared with full MaterialX or OSL execution
- no full wavefront GPU transport path yet
- no complete spectral transport pipeline
- transmission, coat, and layered PBR behavior are still being refined
- no full production-ready subdivision/tessellation path yet

If you need the current transport and backend boundaries in more detail, read `docs/architecture.md`.

## Repository Layout

- `include/shiro/render`, `src/render`, `src/runtime`
  - renderer facade, scene/runtime types, frame buffers, environment maps, samplers
- `include/shiro/backend`, `src/backend`
  - backend interfaces and implementations
  - `cpu/` contains the Embree-backed reference path tracer
  - `optix/` contains the CUDA/OptiX backend
- `include/shiro/hydra`, `src/hydra`
  - Hydra render delegate, render pass, render param, scene translation, prim adapters
- `src/frontend`
  - shared-library packaging for the Hydra plugin
- `plugins`
  - USD plugin metadata and renderer registration JSON
- `tools`
  - local utilities such as the OptiX smoke probe and Houdini environment wrapper
- `testscenes`
  - small USD scenes for renderer validation
- `docs`
  - architecture notes, build notes, research notes, and roadmap

## Supported And Validated Environments

### Linux

Validated on this machine:

- AlmaLinux 9
- Houdini 20.5.410
- SideFX USD 24.03 (`PXR_VERSION 2403`)
- GCC 11.5.0
- CMake 3.26.5
- Ninja 1.13.0
- Embree
- CUDA toolkit 13.0
- NVIDIA driver `580.76.05`
- vendored OptiX 9.0 headers under `external/optix-sdk-9.0.0`

### Windows

Validated on the earlier standalone development path:

- Visual Studio 2022
- vcpkg-based OpenUSD setup
- debug and release library/plugin builds

## Dependency Policy

Shiro is intended to remain DCC-independent at the renderer core level.
Hydra is the integration boundary, not the center of the dependency model.

Current policy:

- Embree is the default CPU acceleration dependency
- CUDA and OptiX are optional unless explicitly required at configure time
- OpenImageIO is preferred as a standalone dependency
- Houdini-bundled libraries are allowed only as development fallback where explicitly supported

Important current detail:

- the repository vendors OptiX 9.0 headers in `external/optix-sdk-9.0.0`
- this is intentional because the local driver stack is `580.76.05`
- newer OptiX headers may configure successfully but fail at runtime with ABI mismatch errors

For more build-specific dependency notes, read `docs/build.md`.

## Quick Start On Linux

### 1. Configure And Build The CPU Path

```bash
cmake --fresh --preset linux-houdini20.5
cmake --build --preset linux-houdini20.5 -j
```

### 2. Configure And Build The CUDA/OptiX Path

```bash
cmake --fresh --preset linux-houdini20.5-cuda
cmake --build --preset linux-houdini20.5-cuda --target hdShiro shiro_optix_probe -j
```

### 3. Run The GPU Smoke Test

```bash
./build-linux-houdini20.5-cuda/tools/shiro_optix_probe
```

This verifies that:

- the CUDA runtime is accessible
- the OptiX runtime can initialize
- the project can compile and launch the current OptiX shader path
- a minimal frame is produced instead of an empty or invalid render

### 4. Launch usdview Or husk With The Correct Environment

The repository includes a small wrapper that prepares the staged plugin path and disables user-site Python packages that can interfere with Houdini's `pxr` module resolution.

Print the required environment:

```bash
./tools/shiro_houdini_env.sh
```

Launch a command directly:

```bash
./tools/shiro_houdini_env.sh /opt/hfs20.5.410/bin/usdview testscenes/minimal.usda
```

If you want to export the variables into the current shell:

```bash
eval "$(./tools/shiro_houdini_env.sh)"
```

The wrapper sets:

- `PYTHONNOUSERSITE=1`
- `HOUDINI_PATH=<build>/stage;&`
- `PXR_PLUGINPATH_NAME=<build>/stage/dso/usd_plugins`

By default it prefers `build-linux-houdini20.5-cuda` and falls back to `build-linux-houdini20.5` when the CUDA stage directory is not present.

## Build Presets

Available top-level configure/build presets:

- `linux-houdini20.5`
  - Linux build against Houdini 20.5.410
- `linux-houdini20.5-cuda`
  - Linux build against Houdini 20.5.410 with CUDA/OptiX required
- `vs2022-x64`
  - Visual Studio 2022 configure preset
- `vs2022-debug`
  - Windows debug build preset
- `vs2022-release`
  - Windows release build preset

The current preset definitions live in `CMakePresets.json`.

## Useful Configure Options

Common options you may want to override:

- `SHIRO_HOUDINI_ROOT`
  - override the Houdini installation root
- `SHIRO_CUDA_ROOT`
  - override the CUDA toolkit root
- `SHIRO_OPTIX_ROOT`
  - override the OptiX SDK header root
- `SHIRO_REQUIRE_CUDA=ON`
  - fail configure if CUDA is not found
- `SHIRO_REQUIRE_OPTIX=ON`
  - fail configure if OptiX is not found
- `SHIRO_REQUIRE_EMBREE=ON`
  - fail configure if Embree is not found
- `SHIRO_REQUIRE_OIIO=ON`
  - fail configure if OpenImageIO is not found
- `SHIRO_ALLOW_HOUDINI_OIIO_FALLBACK=OFF`
  - disable fallback to Houdini-bundled OIIO

Examples:

```bash
cmake --fresh --preset linux-houdini20.5 \
  -DSHIRO_EMBREE_ROOT=/path/to/embree-root
```

```bash
cmake --fresh --preset linux-houdini20.5-cuda \
  -DSHIRO_REQUIRE_CUDA=ON \
  -DSHIRO_REQUIRE_OPTIX=ON
```

```bash
cmake --fresh --preset linux-houdini20.5 \
  -DOpenImageIO_DIR=/path/to/oiio/lib/cmake/OpenImageIO \
  -DSHIRO_REQUIRE_OIIO=ON \
  -DSHIRO_ALLOW_HOUDINI_OIIO_FALLBACK=OFF
```

## Output Layout

After a successful Linux build, the important outputs are:

- `build-linux-houdini20.5/stage/dso/usd/hdShiro.so`
- `build-linux-houdini20.5/stage/dso/usd_plugins/plugInfo.json`
- `build-linux-houdini20.5/stage/dso/usd_plugins/hdShiro/resources/plugInfo.json`
- `build-linux-houdini20.5/stage/UsdRenderers.json`
- `build-linux-houdini20.5-cuda/tools/shiro_optix_probe`

Windows outputs include:

- `build-vs2022/Debug/shiro_core.lib`
- `build-vs2022/Debug/hdShiro.dll`
- `build-vs2022/Release/shiro_core.lib`
- `build-vs2022/Release/hdShiro.dll`

## Hydra Usage Notes

### Render Settings Namespace

Shiro reads render settings under the `shiro:*` namespace.
If a USD `RenderSettings` prim uses un-namespaced attributes such as `samplesPerPixel` or `backend`, Shiro will not consume them.

Examples:

- `shiro:backend`
- `shiro:samplesPerPixel`
- `shiro:samplesPerUpdate`
- `shiro:domeLightSamples`
- `shiro:maxDepth`
- `shiro:diffuseDepth`
- `shiro:specularDepth`
- `shiro:backgroundVisible`
- `shiro:headlightEnabled`

`plugins/UsdRenderers.json` marks `shiro:*` changes as restart-render settings so Hydra clients can re-trigger rendering when they are edited.

### Renderer Variants

The staged `hdShiro` library now registers three renderer entries for Houdini/Solaris:

- `HdShiroRendererPlugin`
  - `Shiro CPU`
- `HdShiroXpuRendererPlugin`
  - `Shiro XPU`
- `HdShiroGpuRendererPlugin`
  - `Shiro GPU`

These are menu-level backend presets backed by the same `hdShiro.so`.
In Solaris, renderer selection is now the primary way to choose CPU, XPU, or GPU.
The low-level `shiro:backend` setting remains supported for Hydra/USD workflows, but the renderer variant wins inside the delegate so stage-authored backend values do not silently override the selected menu entry.

### Backends

Current backend selection values are exposed through render settings and internal enums:

- CPU
- GPU
- Hybrid

The GPU path is available only when the CUDA/OptiX backend is present and the runtime initializes successfully.

## Included Test Scenes

The repository includes a small set of scenes that are useful for targeted validation:

- `testscenes/minimal.usda`
  - minimal scene for plugin bring-up
- `testscenes/sphere_material_dome.usda`
  - simple sphere and dome-light material test
- `testscenes/sphere_material_dome_shiro_high.usda`
  - higher `shiro:*` settings for convergence and accumulation testing
- `testscenes/standard_surface_full.usda`
  - broader `standard_surface` parameter coverage
- `testscenes/standard_surface_grid.usda`
  - grid of material variants for quick look comparison
- `testscenes/transmission_debug.usda`
  - dedicated glass / refraction sanity-check scene

Example:

```bash
./tools/shiro_houdini_env.sh /opt/hfs20.5.410/bin/husk \
  -R HdShiroRendererPlugin \
  -f 1 \
  -o /tmp/shiro_debug.exr \
  testscenes/transmission_debug.usda
```

Use `-R HdShiroXpuRendererPlugin` or `-R HdShiroGpuRendererPlugin` to force the other backend variants.

## Current Feature Coverage

### CPU backend

The CPU renderer currently acts as the reference transport path:

- Embree-backed triangle traversal
- direct lighting from directional lights and dome lights
- path tracing over diffuse, specular, coat, sheen, subsurface-like diffuse wrap, and transmission approximations
- dome-light HDRI sampling
- accumulation into beauty/albedo/normal/depth buffers

### OptiX backend

The OptiX path currently supports:

- triangle GAS build and traversal
- directional lights
- dome lights and HDRI environment evaluation
- primary path tracing for translated PBR material parameters
- beauty/albedo/normal/depth AOV output
- host-side readback for Hydra output buffers

## Known Limitations

The current repository still has important renderer limitations:

- material graphs are not fully texture-driven yet
- layered material behavior is still approximate in several cases
- explicit caustics handling is not implemented
- the transmission path is still under active refinement
- subdivision controls are not yet a full production subdivision/tessellation implementation
- the OptiX backend is not yet a full replacement for the CPU reference path in every scene type

In short:

- use the CPU backend as the reference when correctness matters most
- use the OptiX backend for active GPU bring-up, Hydra integration, and early performance/image-quality work

## Documentation Map

- `docs/build.md`
  - environment assumptions, package notes, and detailed build notes
- `docs/architecture.md`
  - module boundaries and execution model
- `docs/research.md`
  - implementation notes and renderer research references
- `docs/roadmap.md`
  - staged roadmap and next major work items
- `docs/codex-handoff.md`
  - persistent project context used during local development sessions

## Status Summary

Shiro already has enough structure to develop a serious renderer inside Hydra:

- the delegate boundary exists
- the runtime scene snapshot exists
- both CPU and GPU transport paths exist
- test scenes and validation tools exist

What remains is the long tail that turns a renderer skeleton into a production renderer:

- broader material fidelity
- better transport strategies
- stronger GPU scheduling
- deeper Hydra feature coverage
- more systematic validation

That is the current focus of the repository.
