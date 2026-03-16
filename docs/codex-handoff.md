# Codex Handoff

Last updated: 2026-03-16

## Purpose

This document is the persistent handoff for future Codex sessions.

When significant research, environment discovery, API investigation, or design
reasoning happens, update this file so a later session in another environment
can reconstruct the current understanding without relying on chat history.

## Project snapshot

- Shiro is still a renderer skeleton, not a full production renderer yet.
- The current working path is:
  - build the Hydra render delegate against Houdini 20.5.410 on AlmaLinux 9
  - make the plugin discoverable from Houdini Solaris and Houdini-bundled
    `usdview`
  - keep the CPU path tracer as the minimal reference backend
- MaterialX / OpenPBR / MoonRay-style architecture are still reference targets,
  not fully implemented features in the current code.

## Environment facts discovered locally

- Host OS used for integration work:
  - AlmaLinux 9
  - kernel `5.14.0-503.23.1.el9_5.x86_64`
- Houdini installs found locally:
  - `/opt/hfs20.0.724`
  - `/opt/hfs20.5.410`
  - `/opt/hfs21.0.440`
- Active integration target:
  - Houdini `20.5.410`
  - local install root: `/opt/hfs20.5.410`
- Houdini 20.5.410 ships SideFX USD 24.03:
  - `/opt/hfs20.5.410/toolkit/include/pxr/pxr.h` defines:
    - `PXR_MINOR_VERSION 24`
    - `PXR_PATCH_VERSION 3`
    - `PXR_VERSION 2403`
- Houdini's `houdini_setup_bash` reports:
  - build compiler `11.2.1`
  - build glibc `2.28`
  - C++ ABI define `_GLIBCXX_USE_CXX11_ABI=1`

## Important integration conclusions

### 1. Houdini SDK must be preferred over generic `pxr` when targeting Solaris/usdview

Reasoning:

- Houdini ships its own USD build and ABI surface.
- Matching against SideFX headers/libraries is safer than building against a
  standalone OpenUSD package and hoping the plugin ABI matches.
- The repository previously targeted `vcpkg` OpenUSD on Windows. That is still
  useful for generic development, but not the primary Linux/Houdini path.

Implementation outcome:

- CMake now prefers Houdini when `SHIRO_HOUDINI_ROOT`, `$HFS`, or known
  `/opt/hfs*` installs are present.
- Fallback to `find_package(pxr)` still exists for non-Houdini environments.

### 2. Houdini 20.5 expects a C++17-compatible plugin build

Reasoning:

- `hcustom --cflags` from `/opt/hfs20.5.410` reports `-std=c++17`.
- Building the plugin with a newer language mode than the host SDK is an
  unnecessary risk unless there is a strong feature requirement.
- Shiro's current source does not require C++20.

Implementation outcome:

- `shiro_core` and `hdShiro` were lowered from `cxx_std_20` to `cxx_std_17`.

### 3. Linux shared-plugin builds require PIC for `shiro_core`

Reasoning:

- `hdShiro.so` links `libshiro_core.a`.
- The first Linux link failed with:
  - `relocation ... can not be used when making a shared object; recompile with -fPIC`

Implementation outcome:

- `POSITION_INDEPENDENT_CODE ON` was added to both `shiro_core` and `hdShiro`.

### 4. Do not depend on `houdini_get_default_install_dir()` in CMake

Reasoning:

- SideFX's `houdini_get_default_install_dir()` calls `hython`.
- In this environment, `hython` fails immediately if the Houdini license
  server is unavailable.
- That means configure/install logic that depends on `hython` is brittle and
  can fail even when compilation itself is fine.

Implementation outcome:

- Shiro stages into a deterministic build-local layout instead:
  - `build-linux-houdini20.5/stage/dso/usd/hdShiro.so`
  - `build-linux-houdini20.5/stage/dso/usd_plugins/...`
- Regular `cmake --install` works without requiring Houdini to be runnable.

### 5. Linux `plugInfo.json` should include the shared-library suffix explicitly

Reasoning:

- A registry probe initially failed to load the plugin when
  `LibraryPath` was `../../usd/hdShiro`.
- The loader tried to open the path literally and did not append `.so`.

Implementation outcome:

- `plugins/hdShiro/plugInfo.json` is now treated as a CMake template.
- CMake generates a platform-specific plugin info file using
  `hdShiro${CMAKE_SHARED_LIBRARY_SUFFIX}`.

### 6. Actual render resolution should come from bound AOV render buffers first

Reasoning:

- For offscreen render workflows (`usdrecord`, `husk`, render products),
  the authoritative image size is often the render buffer allocation, not the
  interactive viewport.
- Relying only on `renderPassState->GetViewport()` is fragile.

Implementation outcome:

- `HdShiroRenderPass::_Execute()` now reads width/height from AOV bindings
  before falling back to viewport size.

### 7. `SceneDelegate->Get(id, HdTokens->points)` is too weak for real Hydra ingestion

Reasoning:

- For Hydra-driven geometry, primvars can arrive through indexed primvar APIs
  and topology should be read through the dedicated scene-delegate accessors.
- Direct property access is more likely to miss data or behave differently
  across delegates.

Implementation outcome:

- Mesh extraction now uses:
  - `GetVisible(id)`
  - `GetIndexedPrimvar(id, HdTokens->points, ...)`
  - `GetMeshTopology(id)`
  - indexed/display primvar fallback for `displayColor`

## Files changed for the Houdini/AlmaLinux path

- `CMakeLists.txt`
- `CMakePresets.json`
- `cmake/ShiroOptions.cmake`
- `cmake/ShiroDependencies.cmake`
- `include/shiro/hydra/RenderDelegate.h`
- `include/shiro/hydra/RendererPlugin.h`
- `include/shiro/hydra/SceneBridge.h`
- `include/shiro/hydra/Tokens.h`
- `src/hydra/RenderDelegate.cpp`
- `src/hydra/RenderParam.cpp`
- `src/hydra/RenderPass.cpp`
- `src/hydra/RendererPlugin.cpp`
- `src/hydra/SceneBridge.cpp`
- `src/hydra/Tokens.cpp`
- `plugins/hdShiro/plugInfo.json`
- `plugins/usd_plugins/plugInfo.json`
- `plugins/UsdRenderers.json`
- `README.md`
- `docs/build.md`
- `docs/research.md`
- `testscenes/minimal.usda`

## Current staged plugin layout

After `cmake --build --preset linux-houdini20.5`:

- `build-linux-houdini20.5/stage/dso/usd/hdShiro.so`
- `build-linux-houdini20.5/stage/dso/usd_plugins/plugInfo.json`
- `build-linux-houdini20.5/stage/dso/usd_plugins/hdShiro/resources/plugInfo.json`
- `build-linux-houdini20.5/stage/UsdRenderers.json`

Meaning:

- `PXR_PLUGINPATH_NAME` can point at `.../stage/dso/usd_plugins`
- `HOUDINI_PATH` can point at `.../stage;&`

## Commands that were verified

Configure:

```bash
cmake --fresh --preset linux-houdini20.5
```

Build:

```bash
cmake --build --preset linux-houdini20.5
```

Install:

```bash
cmake --install build-linux-houdini20.5 --prefix /tmp/shiro-install
```

The install step succeeded and produced:

- `/tmp/shiro-install/dso/usd/hdShiro.so`
- `/tmp/shiro-install/dso/usd_plugins/plugInfo.json`
- `/tmp/shiro-install/dso/usd_plugins/hdShiro/resources/plugInfo.json`
- `/tmp/shiro-install/UsdRenderers.json`

## Verification that was completed

### Plugin registry discovery

A small C++ probe linked against Houdini's USD libraries successfully:

- registered `build-linux-houdini20.5/stage/dso/usd_plugins`
- found `HdShiroRendererPlugin`

Observed result:

- `found`

### Render delegate factory path

A second C++ probe successfully:

- loaded `HdShiroRendererPlugin`
- called `CreateRenderDelegate()`
- queried supported prim types

Observed result:

- `1 2 1`

Interpretation:

- `1` supported Rprim type: mesh
- `2` supported Sprim types: camera, material
- `1` supported Bprim type: renderBuffer

This is strong evidence that:

- the plugin is loadable
- the shared-library dependencies resolve
- the factory registration path is valid

## Verification that is still blocked

### Houdini-bundled `usdview` / `usdrecord` / `husk`

These could not be fully validated in this environment because the Houdini
license stack is not available.

Observed behavior:

- `usdview`, `usdrecord`, and `testusdview` are `#!/usr/bin/env hython`
  scripts in the Houdini install.
- `hython` fails here with:
  - failed to start or connect to `hserver`
  - no licenses could be found

Implication:

- A real GUI or offline image render from Houdini tools has not yet been
  confirmed in this environment.
- The plugin loadability and delegate creation are verified, but final image
  validation in Solaris/usdview is still pending.

## Solaris freeze investigation

User-reported observation from a real Houdini session:

- `PXR_PLUGINPATH_NAME` was set correctly
- Houdini started successfully
- `HdShiro` appeared in the Solaris renderer list
- switching the viewport renderer to Shiro caused Houdini to freeze
- no useful terminal stdout/stderr was emitted

### Root cause analysis

The strongest root cause was the original execution model in the render
delegate:

- `HdShiroRenderPass::_Execute()` called `HdShiroRenderParam::Render()`
- `HdShiroRenderParam::Render()` performed a full-frame render synchronously
- the CPU renderer used brute-force triangle intersection and full-frame
  path tracing
- default settings were still relatively expensive for an interactive viewport:
  - `1280x720`
  - `8 spp`
  - `maxDepth = 6`
- all of that happened in Hydra's execute path, which is effectively the worst
  place to block when driving a DCC viewport

Practical effect:

- even if the renderer was technically "working", Solaris would look frozen
  because the delegate blocked the render/viewport thread until the whole frame
  completed
- on non-trivial scenes, that can be many seconds or much longer
- because there was no logging and no async progress, it looked like a hard
  freeze

### Related secondary issues

- old render jobs were not cancelable when the camera moved or the viewport
  resized
- all CPU cores were used, leaving little room for the host UI
- render buffers assumed exact framebuffer-size matches, which is unsafe once
  async rendering returns stale or previous frames during resize transitions
- hidden or invalid meshes could remain in the render-param cache because
  `HdShiroMesh::Sync()` only inserted meshes and did not remove stale ones when
  extraction failed

### Implemented fix

The delegate was changed to an asynchronous, cancelable model:

- `HdShiroRenderParam` now owns a worker thread
- `HdShiroRenderPass::_Execute()` now:
  - updates image size
  - builds the camera
  - queues a render request
  - copies the latest completed framebuffer if one exists
  - returns quickly instead of waiting for the whole frame render
- in-flight renders are canceled when:
  - camera changes
  - render settings change
  - image size changes
  - scene contents change
  - pause/stop paths are used
- viewport defaults were lowered to:
  - `1 spp`
  - `maxDepth = 3`
- the core renderer now leaves one CPU thread free by default instead of taking
  every reported hardware thread

Files directly involved in this fix:

- `include/shiro/render/Renderer.h`
- `src/render/Renderer.cpp`
- `include/shiro/hydra/RenderParam.h`
- `src/hydra/RenderParam.cpp`
- `src/hydra/RenderPass.cpp`
- `src/hydra/RenderDelegate.cpp`
- `src/hydra/RenderBuffer.cpp`
- `src/hydra/Mesh.cpp`

### Expected behavior after the fix

- switching to Shiro should no longer hard-block the Solaris UI waiting for a
  full frame to finish
- camera motion / viewport resize should cancel stale renders rather than
  finishing work the user no longer cares about
- first visible image quality is intentionally low but fast

### What is still not proven here

- a real Solaris viewport session has not been rerun from this environment
  because the local machine still cannot execute Houdini's `hython` tools due
  licensing
- so this fix is strongly grounded in code-path analysis and Hydra behavior,
  but the final user-side confirmation still needs to be performed in the
  licensed Houdini environment

## Current renderer capability limits

These are important so future sessions do not overestimate the current state.

- CPU path tracer only
- brute-force triangle intersection
- no Embree / OptiX integration yet
- no true USDShade / MaterialX / OpenPBR material-network translation yet
- materials are still inferred mostly from mesh primvars / simple attributes
- no texture system
- no volume rendering
- no checkpoint / resume
- no progressive multi-iteration renderer state beyond the minimal Hydra pass

## Reasoning about Solaris readiness

What is ready:

- the delegate can be discovered from USD plugin registries
- Solaris should be able to list the renderer because `UsdRenderers.json`
  exists at the Houdini package root
- build/install layout now matches Houdini expectations

What is not yet proven:

- final viewport image update inside Solaris
- husk/usdview final image generation under a working license
- material network fidelity for anything beyond simple display-color-driven
  shading

## Next recommended work

Priority order:

1. Restore a valid Houdini license environment and perform an end-to-end test:
   - `usdview --renderer HdShiroRendererPlugin testscenes/minimal.usda`
   - Solaris viewport selection
   - optional `usdrecord` / `husk`
2. Replace brute-force intersection with Embree.
3. Implement explicit light sampling and MIS.
4. Replace display-color-only material ingestion with real USDShade /
   MaterialX / OpenPBR translation.
5. Add render stats / cancellation / progressive updates expected of a real
   interactive delegate.

## Source references used during this phase

- SideFX HDK compile docs:
  - https://www.sidefx.com/docs/hdk/_h_d_k__intro__compiling.html
- SideFX USD/Hydra docs:
  - https://www.sidefx.com/docs/hdk/_h_d_k__u_s_d_hydra.html
- OpenUSD Hydra getting started:
  - https://openusd.org/release/api/_page__hydra__getting__started__guide.html
- MaterialX:
  - https://materialx.org/
- OpenPBR:
  - https://academysoftwarefoundation.github.io/OpenPBR/
- MoonRay docs:
  - https://docs.openmoonray.org/
- OpenQMC:
  - https://github.com/MomentsInGraphics/openqmc

## Solaris image-output milestone work

Date: 2026-03-16

User-side status before this phase:

- `HdShiro` already appeared in Solaris renderer selection
- switching to the renderer no longer froze the UI after the previous async fix
- next requested milestone was:
  - a sphere can render
  - a bound material affects shading
  - a dome light affects the image
  - camera motion updates the image
  - renderer-specific render settings can be changed from Solaris

### What was missing in code before this phase

Hydra ingest was still too incomplete for a real Solaris lookdev loop:

- only `mesh` rprims were consumed
- supported sprims were only:
  - `camera`
  - `material`
- but `material` sprims were effectively ignored because `HdShiroMaterial::Sync()`
  was a no-op
- there was no light sprim support at all
- mesh sync baked a fallback material from `displayColor`, but did not respect a
  bound material network
- `GetMaterialRenderContexts()` returned only `shiro`

That last point matters. If the render delegate advertises only a renderer-
specific material context, Solaris/UsdImaging may not hand back a generic
surface material network for ordinary `UsdPreviewSurface` / MaterialX materials.
For interoperability, the delegate must also advertise the universal
USDShade render context.

### Concrete API findings from Houdini 20.5.410 / USD 24.03

Verified directly from SideFX USD headers in `/opt/hfs20.5.410/toolkit/include`:

- `HdSceneDelegate::GetMaterialId(SdfPath const&)`
- `HdSceneDelegate::GetMaterialResource(SdfPath const&)`
- `HdSceneDelegate::GetLightParamValue(SdfPath const&, TfToken const&)`
- `HdLightTokens` includes:
  - `color`
  - `intensity`
  - `exposure`
  - `texture:file`
- `HdMaterialNetworkMap` can be converted via `HdConvertToHdMaterialNetwork2(...)`
- `HdMaterialTerminalTokens->surface` is the correct terminal to follow for
  the primary surface shader

### Implemented changes

#### 1. Material binding and material-network translation

Files:

- `src/hydra/Mesh.cpp`
- `src/hydra/Material.cpp`
- `include/shiro/hydra/SceneBridge.h`
- `src/hydra/SceneBridge.cpp`
- `include/shiro/hydra/RenderParam.h`
- `src/hydra/RenderParam.cpp`

Behavior now:

- mesh sync stores:
  - geometry
  - a fallback material from mesh primvars/simple authored attributes
  - the bound material path from `GetMaterialId()`
- material sprim sync pulls `GetMaterialResource()` and parses:
  - `UsdPreviewSurface`
  - MaterialX `standard_surface`
  - OpenPBR surface nodes
- translation currently supports constant surface-node parameters only:
  - base color
  - metallic / metalness
  - roughness / specular roughness
  - emissive color / emission strength
  - ior

Important limitation:

- connected texture nodes are still ignored
- utility-node subgraphs are still ignored
- this is sufficient for the requested first Solaris milestone, but not yet for
  production material fidelity

#### 2. Dome light and distant light ingestion

Files:

- `include/shiro/hydra/Light.h`
- `src/hydra/Light.cpp`
- `src/hydra/RenderDelegate.cpp`
- `src/hydra/SceneBridge.cpp`
- `src/hydra/RenderParam.cpp`
- `include/shiro/render/Renderer.h`
- `src/render/Renderer.cpp`

Behavior now:

- supported sprim types now include:
  - `domeLight`
  - `distantLight`
- dome lights contribute uniform environment radiance using:
  - `color * intensity * 2^exposure`
- distant lights contribute directional lighting using the authored light
  transform and the same radiance formula

Current limitation:

- dome-light textures (`texture:file`) are not sampled yet
- for now, authored dome lights behave as uniform-color infinite lights
- that is enough to make a Solaris sphere/material/domelight scene visibly
  render, but HDRI fidelity is still future work

#### 3. Render settings surfaced for Solaris

Files:

- `include/shiro/hydra/Tokens.h`
- `src/hydra/Tokens.cpp`
- `src/hydra/RenderDelegate.cpp`
- `src/hydra/RenderParam.cpp`
- `include/shiro/render/Renderer.h`
- `src/render/Renderer.cpp`

Exposed settings:

- `shiro:samplesPerPixel`
- `shiro:maxDepth`
- `shiro:diffuseDepth`
- `shiro:specularDepth`
- `shiro:backgroundVisible`
- `shiro:headlightEnabled`
- `shiro:threadLimit`

Why these settings were chosen:

- `samplesPerPixel`, `maxDepth`, `diffuseDepth`, and `specularDepth` are the
  smallest common path-tracing controls shared across Karma, MoonRay,
  Redshift, and RenderMan-style workflows
- `backgroundVisible` is operationally necessary once dome/environment lighting
  is involved
- `headlightEnabled` is a bring-up convenience so unlit stages still show
  something while testing
- `threadLimit` is a practical interactive control for the current CPU backend

Note on provenance:

- Karma, MoonRay, and Redshift alignment was based on official renderer docs
- RenderMan alignment here is an inference from the same common path-tracing
  control structure rather than a strict one-to-one copied UI

#### 4. Material render-context fix

File:

- `src/hydra/RenderDelegate.cpp`

This was a subtle but important fix:

- `GetMaterialRenderContexts()` now returns:
  - `UsdShadeTokens->universalRenderContext`
  - `shiro`

Without the universal context, Solaris may not provide a generic material
network for standard USDShade materials. This change is required for
interoperability.

### Renderer behavior changes

The core CPU renderer was extended to match the new interactive settings:

- separate diffuse/specular bounce limits are now enforced
- background visibility only affects primary rays; environment light still
  contributes on later bounces
- thread count can now be capped with `shiro:threadLimit`
- authored dome lights now replace the old implicit gradient background as the
  scene environment
- when no scene lights exist and `shiro:headlightEnabled` is true, Shiro falls
  back to:
  - one default distant light
  - the old gradient environment

### New test scene

Added:

- `testscenes/sphere_material_dome.usda`

Purpose:

- minimal repro scene for:
  - sphere geometry
  - bound `UsdPreviewSurface`
  - dome light
  - camera

This scene is intended for:

- `usdview --renderer HdShiroRendererPlugin`
- Solaris viewport smoke tests

### Validation performed

Verified locally:

- `cmake --build build-linux-houdini20.5 -j8`
  - success after adding light sprims, material parsing, and render-setting
    expansion

Not yet validated from this environment:

- actual Solaris viewport image with the new material/light path
- actual `usdview` GUI rendering with the new test scene

Reason:

- this environment still cannot launch the Houdini Python-based frontends due
  license constraints

### Expected first user-side validation path

In licensed Houdini/Solaris:

1. switch renderer to `HdShiro`
2. create:
   - sphere
   - material
   - dome light
3. confirm:
   - sphere is visible
   - material color/roughness changes affect the image
   - dome light affects illumination
   - tumbling the camera triggers cancel/re-render without UI freeze
4. test renderer settings:
   - `shiro:samplesPerPixel`
   - `shiro:maxDepth`
   - `shiro:diffuseDepth`
   - `shiro:specularDepth`
   - `shiro:backgroundVisible`
   - `shiro:headlightEnabled`

### Known remaining limitations after this phase

- no texture-node evaluation
- no MIS between BSDF and dome/direct-light sampling yet
- dome HDRI support is now implemented only for:
  - `latlong`
  - `angular`
  - `automatic` fallback to `latlong`
- no transmission / refraction transport even though some material parameters
  are parsed
- no Embree acceleration yet
- no volume support

So this phase should be treated as:

- "Solaris minimum visible lookdev milestone"

not yet:

- "production-ready shading or lighting fidelity"

## Solaris full-frame red-noise regression

Date: 2026-03-16

User-side symptom:

- assigning a red MaterialX `standard_surface` to a sphere and placing a dome
  light produced a viewport image that was almost entirely red/noisy
- the GL viewport showed the expected sphere framing, but Shiro looked like the
  camera was inside or extremely close to the object

### Root cause

The main bug was not the path tracer transport itself. It was camera ingestion.

`HdShiroCamera::Sync()` had been implemented as a no-op:

- it did not call `HdCamera::Sync()`
- therefore the Hydra camera sprim held by the render-pass state was never
  populated with the real transform / focal length / aperture from Solaris
- as a result, `HdRenderPassState` camera-derived matrices could fall back to
  effectively default/identity camera data
- in practice this can place Shiro near the origin looking with a default
  frustum, which makes a unit sphere at the origin fill the whole frame and
  appear as an all-red noisy image

This is why the symptom looked like a path-tracing bug while actually being a
camera-state bug at the Hydra boundary.

### Implemented fix

Files:

- `src/hydra/Camera.cpp`
- `src/hydra/SceneBridge.cpp`

Changes:

- `HdShiroCamera::Sync()` now delegates to `HdCamera::Sync()`
- `HdShiroCamera::GetInitialDirtyBitsMask()` now returns `HdCamera::AllDirty`
- `HdShiroSceneBridge::BuildCamera()` now prefers the populated `HdCamera`
  object from `HdRenderPassState::GetCamera()` and derives:
  - world transform
  - orientation vectors
  - perspective FOV from aperture/focal-length
- only if that path is unavailable or invalid does it fall back to the older
  world-to-view / projection matrix path

### Why this fix is the right level

- if the camera sprim is wrong, any transport implementation will render the
  wrong image
- fixing camera synchronization is a prerequisite before diagnosing deeper
  sampling or integrator issues
- after this fix, if artifacts remain, they are much more likely to be genuine
  renderer issues instead of Hydra state ingestion problems

## Progressive accumulation and dome HDRI implementation

Date: 2026-03-16

User-side motivation:

- the viewport noise was not reducing over time
- dome lights needed real HDRI texture support for `.exr` / `.hdr`
- render settings needed to expose interactive accumulation controls similar in
  spirit to Karma / MoonRay / RenderMan / Redshift

### Important root-cause conclusion

The renderer was still not behaving like an interactive progressive renderer.

Before this phase:

- Hydra triggered an async render job
- but each job rendered a fixed frame once and then stopped
- with `samplesPerPixel = 1`, that meant the viewport stayed forever at the
  first noisy sample
- worse, if accumulation had been naively repeated without advancing
  `sampleIndex`, OpenQMC would have replayed the same sample pattern and still
  looked "stuck"

Implementation consequence:

- progressive rendering must carry an accumulation state across Hydra executes
- each batch must advance the sampler seed with `sampleStart`
- convergence must be expressed as:
  - accumulated samples so far
  - target `samplesPerPixel`
  - per-update batch size `samplesPerUpdate`

### Implemented renderer changes

Files:

- `include/shiro/render/EnvironmentMap.h`
- `src/render/EnvironmentMap.cpp`
- `include/shiro/render/Renderer.h`
- `src/render/Renderer.cpp`
- `include/shiro/render/Types.h`

Core behavior changes:

- added `Renderer::RenderSampleBatch(scene, camera, sampleStart, sampleCount)`
- added `Renderer::FrameAccumulator`
- sampler seeds now use:
  - pixel x/y
  - global progressive `sampleIndex = sampleStart + localBatchSample`
- the renderer worker can now publish partial frames between updates instead of
  only a final frame

Environment map implementation:

- the correct architectural target is standalone OpenImageIO as a Shiro-owned dependency
- the build system now prefers standalone `OpenImageIO`
- Houdini-bundled OIIO is treated only as a development fallback when:
  - `SHIRO_ALLOW_HOUDINI_OIIO_FALLBACK=ON`
  - no standalone OIIO package is found
- in this local environment the fallback still resolves to:
  - include path: `/opt/hfs20.5.410/toolkit/include/OpenImageIO`
  - runtime libs:
    - `/opt/hfs20.5.410/dsolib/libOpenImageIO_sidefx.so`
    - `/opt/hfs20.5.410/dsolib/libOpenImageIO_Util_sidefx.so`
- `ldd build-linux-houdini20.5/hdShiro.so` confirms both fallback OIIO libraries resolve in this environment

Implemented dome-texture scope:

- `inputs:texture:file`
- `inputs:texture:format`
- supported mappings:
  - `latlong`
  - `angular`
- current fallback:
  - `automatic -> latlong`
- not yet implemented as true mappings:
  - `mirroredBall`
  - `cubeMapVerticalCross`

Lighting/transport changes:

- miss shader now evaluates authored dome textures instead of only a flat dome
  color
- explicit direct dome-light sampling was added for diffuse events
- environment/emissive contributions are only added on:
  - primary rays
  - paths whose previous event was specular
- this avoids the most obvious double counting while MIS is still absent

### Implemented Hydra/render-setting changes

Files:

- `src/hydra/SceneBridge.cpp`
- `include/shiro/hydra/RenderParam.h`
- `src/hydra/RenderParam.cpp`
- `include/shiro/hydra/Tokens.h`
- `src/hydra/Tokens.cpp`
- `src/hydra/RenderDelegate.cpp`
- `cmake/ShiroDependencies.cmake`
- `cmake/ShiroOptions.cmake`
- `CMakeLists.txt`

Hydra changes:

- dome lights now ingest:
  - `HdLightTokens->textureFile`
  - `HdLightTokens->textureFormat`
  - light transform basis for environment orientation
- material parsing was widened slightly for constant-parameter support:
  - MaterialX `standard_surface` transmission parameters
  - OpenPBR transmission parameters

Render settings added:

- `shiro:samplesPerPixel`
- `shiro:samplesPerUpdate`
- `shiro:domeLightSamples`
- `shiro:maxDepth`
- `shiro:diffuseDepth`
- `shiro:specularDepth`
- `shiro:backgroundVisible`
- `shiro:headlightEnabled`
- `shiro:threadLimit`

Dependency-policy changes:

- added `SHIRO_ENABLE_OIIO`
- added `SHIRO_REQUIRE_OIIO`
- added `SHIRO_ALLOW_HOUDINI_OIIO_FALLBACK`
- CMake now emits which OIIO provider is being used:
  - `standalone`
  - `houdini`
  - `none`
- strict standalone-OIIO configuration is now expressible as:
  - `SHIRO_REQUIRE_OIIO=ON`
  - `SHIRO_ALLOW_HOUDINI_OIIO_FALLBACK=OFF`

Current defaults in the delegate:

- `samplesPerPixel = 32`
- `samplesPerUpdate = 1`
- `domeLightSamples = 1`
- `maxDepth = 4`

### Validation completed

Verified locally:

- `cmake --build build-linux-houdini20.5 -j8`
  - success
- build definitions now include:
  - `SHIRO_HAVE_OIIO=1`
- runtime dependency check:
  - `ldd build-linux-houdini20.5/hdShiro.so`
  - confirms:
    - `libOpenImageIO_sidefx.so.2.3`
    - `libOpenImageIO_Util_sidefx.so.2.3`

### Remaining risks after this phase

- no MIS yet, so high-contrast HDRIs can still be noisier than production
  renderers
- no texture graph/material image-node evaluation yet
- dome texture mappings other than latlong/angular still fall back
- no Embree acceleration, so sample-rate increases will hit brute-force
  geometry cost quickly

## Slow renderer-start investigation

Date: 2026-03-16

User-side symptom:

- after switching the viewport renderer to Shiro, there was a long delay before
  the first render actually started

Root-cause analysis from the current code:

- `HdShiroMesh::Sync()` rebuilt mesh payloads unconditionally, ignoring Hydra
  dirty bits
- that meant points/topology/transform triangulation and world-space baking
  could rerun even when only material binding changed
- `HdShiroRenderParam::WorkerLoop()` also rebuilt a full `Scene` snapshot every
  progressive batch, which is especially bad now that accumulation updates can
  happen many times per frame

Implemented mitigation:

- `HdShiroMesh` now caches the last extracted mesh payload and only re-extracts
  on geometry-relevant dirty bits:
  - `DirtyPoints`
  - `DirtyTopology`
  - `DirtyTransform`
  - `DirtyPrimvar`
  - `DirtyVisibility`
- material-binding-only changes now reuse the cached mesh payload
- `HdShiroRenderParam` now caches the built `Scene` snapshot and reuses it
  across progressive batches until the scene is invalidated
- environment-map resolution now happens against that cached scene snapshot
  instead of reloading/rebinding through a freshly rebuilt scene copy each
  batch

Files:

- `include/shiro/hydra/Mesh.h`
- `src/hydra/Mesh.cpp`
- `include/shiro/hydra/RenderParam.h`
- `src/hydra/RenderParam.cpp`

Expected effect:

- shorter delay between renderer switch and first visible frame
- lower CPU overhead during interactive progressive updates
- less repeated work when the scene is stable and only accumulation is
  advancing

## Embree integration and upside-down image fix

Date: 2026-03-16

User-side request:

- Embree must be used, not optional in practice
- the rendered image appeared vertically inverted in the Solaris viewport

Environment facts discovered during integration:

- AlmaLinux 9 package availability:
  - `embree-3.13.5-4.el9`
  - `embree-devel-3.13.5-4.el9`
- after installation, runtime library resolution shows:
  - `/lib64/libembree3.so.3`
- `pkg-config` metadata is not present for this package on the local machine
- therefore CMake must not rely on `pkg-config` for Embree detection

Important header-resolution trap:

- Houdini 20.5.410 also ships headers at:
  - `/opt/hfs20.5.410/toolkit/include/embree3`
- those headers are SideFX-wrapped and place Embree symbols in a private
  namespace
- if source includes `<embree3/rtcore.h>`, the Houdini include path can win
  and the build silently targets the wrong header set

Implemented resolution:

- CMake now resolves Embree headers as the concrete `embree3` directory by
  searching for `rtcore.h` with `PATH_SUFFIXES embree3`
- renderer code includes `<rtcore.h>` directly
- this makes the standalone `embree-devel` headers win over SideFX's wrapper
- `SHIRO_REQUIRE_EMBREE=ON` remains the intended default behavior
- `SHIRO_EMBREE_ROOT` is supported for extracted/manual Embree roots

Renderer implementation outcome:

- `Renderer` now owns a cache of the last built Embree acceleration scene,
  keyed by the stable `Scene*` snapshot pointer
- BVH build happens once per scene snapshot, not once per progressive sample
  batch
- triangle intersection and shadow-occlusion tests now use Embree when
  available
- the old brute-force triangle traversal remains only as a non-Embree fallback

Files:

- `cmake/ShiroDependencies.cmake`
- `CMakeLists.txt`
- `include/shiro/core/Config.h`
- `include/shiro/render/Renderer.h`
- `src/render/Renderer.cpp`
- `docs/build.md`
- `docs/research.md`

Validation completed:

- `cmake --fresh --preset linux-houdini20.5`
  - success
  - reports `Shiro Embree provider: system`
- `cmake --build --preset linux-houdini20.5 -j8`
  - success
- `ldd build-linux-houdini20.5/hdShiro.so | rg 'embree|OpenImageIO|not found'`
  - confirms:
    - `libembree3.so.3`
    - `libOpenImageIO_sidefx.so.2.3`
    - `libOpenImageIO_Util_sidefx.so.2.3`

Image-orientation fix:

- primary ray generation previously used:
  - `camera.up * (-filmY * tanHalfFov)`
- it now uses:
  - `camera.up * (filmY * tanHalfFov)`

Reasoning:

- the symptom was a vertical presentation mismatch, not evidence that the path
  tracer itself was fundamentally incorrect
- the simplest coherent fix is to align Shiro's raster convention with the
  viewport path instead of altering transport or AOV memory layout

Remaining risks after this phase:

- no MIS yet for dome-light sampling
- Embree build quality is currently `RTC_BUILD_QUALITY_LOW`, chosen to favor
  interactive startup over final-frame BVH quality
- no instancing, motion blur, or non-triangle Embree geometry yet

## XPU / CUDA status check (2026-03-16)

Reason for this note:

- the user explicitly asked whether Shiro is already an XPU renderer using both
  CPU and GPU, and requested CUDA

Current implementation status:

- Shiro is not yet an actual CPU+GPU XPU renderer
- `BackendKind` exists in `include/shiro/render/Renderer.h` with:
  - `Cpu`
  - `Gpu`
  - `Hybrid`
- that enum is currently only a configuration surface / future-facing API shape
- the active render path in `src/render/Renderer.cpp` is still CPU-only
- the frame render loop launches `std::thread` workers over image rows and runs
  path tracing on the host CPU
- Embree is the only production intersection backend currently wired into
  Shiro's renderer
- there are no Shiro-owned CUDA kernels, no `.cu` compilation units, no
  `enable_language(CUDA)` in the top-level build, and no OptiX integration in
  the current runtime path

Important distinction:

- the repository contains CUDA-related code under `external/openqmc`
- this does not mean Shiro itself is currently rendering on the GPU
- OpenQMC is vendored as a sampling dependency and includes optional GPU-aware
  utilities, but Shiro's renderer does not dispatch its transport, shading, or
  traversal through CUDA today

Evidence captured locally:

- `include/shiro/render/Renderer.h`
  - `BackendKind` is declared
  - default settings still select `BackendKind::Hybrid`
- `src/render/Renderer.cpp`
  - rendering is executed through CPU threads, not CUDA launches
- `CMakeLists.txt`
  - project languages are `CXX` only
  - no top-level CUDA language enablement or CUDA-linked Shiro target exists
- `README.md`
  - still describes a `future XPU execution model`

Local machine readiness check:

- `nvcc --version`
  - failed with `command not found`
- `nvidia-smi`
  - failed because the NVIDIA driver is not available to the current
    environment

Implication:

- even if a CUDA backend were started immediately, this machine is not currently
  in a state where it can compile and validate that backend end-to-end

Correct architectural interpretation:

- today Shiro is a CPU renderer with a Hydra delegate and future-facing backend
  abstraction
- it is not honest to describe the current implementation as XPU
- the correct path to "use CUDA" is to add a real GPU backend, most likely
  OptiX/CUDA for traversal + wavefront/path-state execution, while keeping the
  current CPU/Embree path as the reference backend

Recommended next phase if CUDA becomes a hard requirement:

- add explicit build options for CUDA/OptiX instead of relying on the current
  placeholder `BackendKind`
- split CPU-only host scene data from GPU-uploadable scene buffers
- define which work is duplicated vs shared between CPU and GPU paths
- keep Hydra ingestion backend-agnostic and move execution choice into the core
  renderer
- only call the renderer "XPU" after CPU and GPU both execute real work on the
  same frame with a defined scheduler

## CUDA / OptiX integration phase (2026-03-16)

User request for this phase:

- reorganize the project into cleaner build/runtime/backend/frontend boundaries
- prepare CUDA toolkit integration on AlmaLinux 9
- add an OptiX/CUDA-backed implementation path

Implemented structural changes:

- top-level CMake now creates a shared interface target:
  - `shiro_project_options`
- source/build layout is split by responsibility:
  - `src/runtime`
  - `src/backend`
  - `src/frontend`
  - `tools`
- `Renderer` is now a thin facade
- CPU rendering lives in:
  - `src/backend/cpu/CpuPathTracer.cpp`
- OptiX rendering bootstrap lives in:
  - `src/backend/optix/OptixBackend.cpp`
  - `src/backend/optix/OptixPrograms.cu`
- frontend packaging for Hydra moved into:
  - `src/frontend/CMakeLists.txt`

Dependency and build changes:

- added CMake options:
  - `SHIRO_ENABLE_CUDA`
  - `SHIRO_REQUIRE_CUDA`
  - `SHIRO_ENABLE_OPTIX`
  - `SHIRO_REQUIRE_OPTIX`
  - `SHIRO_CUDA_ROOT`
  - `SHIRO_OPTIX_ROOT`
- CUDA is resolved with `find_package(CUDAToolkit)`
- OptiX is resolved by locating `optix.h`
- compile-time feature macros now include:
  - `SHIRO_HAVE_CUDA`
  - `SHIRO_HAVE_OPTIX`
- added preset:
  - `linux-houdini20.5-cuda`

System state discovered on this machine:

- installed packages:
  - `cuda-toolkit-13.0.0-1.x86_64`
  - `nvidia-sdk-optix-9.1.0-1.el9.noarch`
- toolkit root:
  - `/usr/local/cuda-13.0`
- active symlink:
  - `/usr/local/cuda`
- OptiX headers:
  - `/usr/include/optix`
- `nvcc` exists under `/usr/local/cuda/bin`
- `nvidia-smi` still fails
- local GPU probe reports:
  - `CUDA_ERROR_NO_DEVICE`

Meaning of that result:

- toolkit installation is complete enough for compile/link
- the remaining blocker for actual GPU execution is NVIDIA driver or GPU
  visibility, not Shiro's build scripts

OptiX backend scope in this phase:

- runtime-compiles a CUDA source file via NVRTC
- creates an OptiX device context, module, raygen program group, pipeline, and
  SBT
- launches a raygen-only pipeline that writes a background gradient into the
  beauty buffer
- does not yet build GAS/IAS or trace triangle geometry

Why the scope stopped there:

- the user asked first for project separation and CUDA/OptiX integration
- a raygen-only bootstrap is the smallest honest implementation that exercises
  CUDA + NVRTC + OptiX end-to-end
- full triangle/hitgroup parity with the CPU backend is a larger phase and
  should be built on top of the new backend boundary

Validation completed:

- `cmake --fresh --preset linux-houdini20.5-cuda`
  - success
  - reports:
    - `Shiro CUDA provider: toolkit`
    - `Shiro OptiX provider: sdk`
- `cmake --build --preset linux-houdini20.5-cuda -j8`
  - success
- `cmake --build --preset linux-houdini20.5 -j8`
  - success
- `ldd build-linux-houdini20.5-cuda/src/frontend/hdShiro.so | rg 'cuda|nvrtc|optix|not found'`
  - confirms linkage to:
    - `libcuda.so.1`
    - `libnvrtc.so.13`
    - `libcudart.so.13`
- `build-linux-houdini20.5-cuda/tools/shiro_optix_probe`
  - currently fails because GPU runtime is unavailable on this host

New operational files:

- `tools/install_cuda_almalinux9.sh`
  - installs `cuda-toolkit` and `nvidia-sdk-optix`
  - optional `--with-driver` path installs `kmod-nvidia-open-dkms`
- `tools/shiro_optix_probe`
  - direct GPU backend smoke test

Frontend/runtime behavior decision:

- `RenderSettings.backend` now defaults to CPU
- GPU/Hybrid remain opt-in
- `Renderer` only routes to the OptiX backend when:
  - GPU backend is reported available
  - the scene is empty of meshes
- this is intentional so incomplete GPU geometry support does not silently break
  current Hydra rendering

Recommended next phase:

- get `nvidia-smi` working on the machine
- extend OptiX backend from raygen-only output to:
  - triangle GAS build
  - miss program for dome/environment
  - closest-hit shading for the current triangle scene format
- after that, expose backend selection more aggressively in Hydra settings
- only then revisit Hybrid/XPU scheduling

### Follow-up runtime check in a different terminal

Additional finding after the previous note:

- in a later terminal session, `nvidia-smi` succeeded
- reported GPU:
  - `NVIDIA GeForce RTX 4080`
- reported driver/runtime:
  - `Driver Version 580.76.05`
  - `CUDA Version 13.0`

That means the earlier `CUDA_ERROR_NO_DEVICE` was environment/session-specific
and should not be treated as the final machine state.

Current runtime blocker after re-testing:

- `tools/shiro_optix_probe` now reaches CUDA initialization but fails during
  context acquisition
- diagnostics show:
  - `CUDA_ERROR_OUT_OF_MEMORY`

Measured state at failure time:

- `nvidia-smi --query-gpu=memory.total,memory.used,memory.free`
  - `16376 MiB total`
  - `15867 MiB used`
  - `68 MiB free`
- `nvidia-smi --query-compute-apps=pid,process_name,used_memory`
  - hidden / already-exited process entry:
    - PID `2319975`
    - `[Not Found]`
    - `13891 MiB`
  - Houdini:
    - PID `2455426`
    - `/opt/hfs20.5.410/bin/houdini-bin`
    - `1249 MiB`

Practical implication:

- Shiro's OptiX bootstrap is now build-validated and runtime-visible on the GPU
- the immediate blocker is VRAM pressure, not toolkit installation and not
  driver absence
- retest the probe after reclaiming GPU memory before making further OptiX
  architecture changes
