# Research Notes

## External interfaces

- Hydra render delegates own primitive creation, render-pass execution, render settings, and AOV contracts. Shiro follows the same separation: `RenderDelegate` creates primitives, `RenderPass` triggers frame execution, and `RenderBuffer` stores AOV output.
- Houdini 20.5.410 ships SideFX USD 24.03 (`PXR_VERSION 2403`), so ABI compatibility with SideFX headers and libraries is the primary integration target on AlmaLinux 9.
- MoonRay remains the clearest production reference for separating scene ingestion, shading/material systems, and backend execution. Shiro mirrors that by isolating Hydra, scene translation, and transport.
- OpenQMC is a strong fit for Shiro because it is designed for quasi-Monte-Carlo sampling with renderer-oriented domain branching and deterministic reuse across CPU/GPU backends.

## Dependency strategy

Base runtime:

- OpenUSD / Hydra
- OpenQMC

Production dependencies to add next:

- Embree 4.x for CPU BVH build and traversal.
- OptiX 9.x for GPU traversal and denoiser interop.
- OpenImageIO for texture systems and `maketx` style pipeline compatibility.
- OpenColorIO for display/view transforms.
- OpenVDB and NanoVDB for volume transport.
- MaterialX and optional OSL for shading graphs and closures.
- oneTBB for tasking and memory arenas.

## Feature staging

Stage 1 in this repository:

- Hydra plugin skeleton.
- CPU path tracer reference backend.
- OpenQMC integration point.
- Mesh ingestion, basic PBR material payload, and AOV plumbing.

Stage 2:

- Embree-backed acceleration structures.
- Material network translation from USDShade and MaterialX.
- Direct-light sampling and MIS.
- Texture system and UDIM support.

Stage 3:

- OptiX wavefront backend.
- XPU queue scheduler with shared scene representation.
- Path guiding and adaptive sampling.
- Production features such as deep output, cryptomatte, motion blur, and volumes.

## Constraints

- Accuracy first means CPU is the reference implementation and GPU follows the same transport contract.
- Stability for large scenes means scene data ownership stays outside Hydra primitives and inside a central render parameter cache.
- Performance means acceleration structures, texture caches, and queue scheduling are designed as pluggable subsystems instead of being fused into Hydra-facing code.

## Solaris viewport milestone

Current minimum interactive target for Solaris/usdview is now:

- `mesh` geometry ingestion with bound material lookup through `HdSceneDelegate::GetMaterialId()`
- `material` sprim ingestion through `HdSceneDelegate::GetMaterialResource()`
- `domeLight` and `distantLight` sprim ingestion through `HdSceneDelegate::GetLightParamValue()`
- camera updates through render-pass state matrices
- renderer-specific namespaced render settings exposed through Hydra descriptors

Important Hydra detail:

- `GetMaterialRenderContexts()` must include the universal USDShade render context, not only a renderer-specific context like `shiro`
- if the delegate only advertises `shiro`, Solaris/UsdImaging may not hand back a generic surface material network for ordinary `UsdPreviewSurface` / MaterialX materials
- Shiro now advertises:
  - `UsdShadeTokens->universalRenderContext`
  - `shiro`

## Material translation scope

Implemented now:

- `UsdPreviewSurface`
- MaterialX `standard_surface`
- OpenPBR surface nodes exposed through Hydra material networks

Current translation scope is intentionally narrow:

- constant scalar/color inputs on the surface node only
- no texture nodes yet
- no connected utility-node graph evaluation yet
- no displacement / volume / coat / subsurface / transmission transport yet

This is enough for Solaris milestone scenes such as:

- sphere with bound preview material
- dome light
- camera tumble/orbit
- render setting changes from Solaris

## Render settings rationale

The current Shiro renderer-specific settings are the smallest common subset
shared by production path tracers:

- `shiro:samplesPerPixel`
- `shiro:samplesPerUpdate`
- `shiro:domeLightSamples`
- `shiro:maxDepth`
- `shiro:diffuseDepth`
- `shiro:specularDepth`
- `shiro:backgroundVisible`
- `shiro:headlightEnabled`
- `shiro:threadLimit`

Reasoning:

- Karma, MoonRay, and Redshift all expose sampling and ray-depth controls as first-class render settings
- RenderMan also uses the same path-tracing control pattern, though exact naming differs by integrator/UI; this mapping is an inference from that common structure
- background visibility is needed in DCC lookdev because dome/environment lighting and what the camera sees are often adjusted independently
- samples-per-update is the interactive control that keeps the viewport responsive while still converging toward a larger total sample target
- dome-light sample count is the minimum direct-light control needed once environment maps are importance-sampled independently from BSDF continuation
- headlight fallback is not a final-frame feature, but it is operationally useful in early delegate bring-up when no authored lights exist
- thread limiting is not an artist-facing shading control, but it is necessary to keep Houdini responsive during interactive preview with the current CPU backend

## Progressive accumulation and HDRI dome lighting

Important implementation conclusion from the first real Solaris tests:

- rendering only a single `samplesPerPixel=1` frame and then stopping does not behave like an interactive path tracer, even if Hydra redraws continue
- the renderer must preserve an accumulation state and advance the sampler's `sampleIndex` across updates, otherwise the viewport either freezes at the first sample or repeats the same low-discrepancy sample pattern forever

Current implementation direction:

- `samplesPerPixel` is the convergence target
- `samplesPerUpdate` is the batch size per Hydra update
- the worker thread accumulates `RenderSampleBatch()` outputs until the target is reached
- sampler seeds are offset by `sampleStart`, so each update adds new QMC samples instead of replaying the first one

Environment-lighting implementation now depends on Houdini's bundled OpenImageIO:

- Houdini 20.5.410 ships OpenImageIO 2.3.14 in `toolkit/include/OpenImageIO` and `dsolib/libOpenImageIO_sidefx.so`
- this is sufficient to load `.exr` and `.hdr` dome textures without introducing another image-IO dependency

Architectural correction after review:

- using Houdini's OIIO directly is not the desired long-term dependency model
- the renderer should own its texture/image dependency stack independently of the DCC host
- Houdini-bundled OIIO is acceptable only as a bring-up fallback while Solaris integration is being validated
- the build now prefers standalone `OpenImageIO` and treats Houdini OIIO as fallback only
- if the project wants strict enforcement, configure with:
  - `SHIRO_REQUIRE_OIIO=ON`
  - `SHIRO_ALLOW_HOUDINI_OIIO_FALLBACK=OFF`

Current dome-light texture scope:

- `inputs:texture:file`
- `inputs:texture:format`
- supported parameterizations:
  - `latlong`
  - `angular`
- `automatic` currently resolves to `latlong`
- unsupported USD dome mappings still need dedicated implementations:
  - `mirroredBall`
  - `cubeMapVerticalCross`

Current transport decision:

- direct dome-light sampling is evaluated explicitly at diffuse events
- environment/emissive hits are only accumulated on primary rays or after specular events
- this is the standard path-tracing split that avoids obvious double counting when using next-event estimation without full MIS yet

What is still missing:

- MIS between BSDF sampling and light sampling
- texture-system features beyond direct image loading:
  - UDIM
  - texture filtering controls
  - color-space policy
  - MaterialX/OpenPBR image-node evaluation

## Hydra bring-up performance note

One important Hydra integration lesson from the first real Solaris runs:

- do not perform full mesh extraction on every `Sync()`
- do not rebuild the complete renderer scene snapshot on every progressive
  batch

Even before Embree or GPU work, those two mistakes alone make renderer startup
and interactive updates feel much slower than they need to.

## Embree integration note

Embree is now the required CPU intersection backend for Shiro's Linux/Houdini
path.

Important implementation detail discovered during integration:

- Houdini 20.5.410 ships a private, namespaced Embree wrapper in
  `toolkit/include/embree3`
- if renderer code includes `<embree3/rtcore.h>` while the Houdini toolkit
  include path comes first, the compiler picks SideFX's wrapper instead of the
  system `embree-devel` headers
- that wrapper remaps symbols into a SideFX namespace and is the wrong API
  surface for a DCC-independent renderer dependency

Resulting build decision:

- CMake resolves the Embree include directory as the concrete `.../embree3`
  folder
- renderer code includes `<rtcore.h>` directly
- this forces the standalone Embree headers to win even when Houdini's include
  path is present for USD/HDK

Runtime design decision:

- the renderer now caches an Embree scene per stable `Scene*` snapshot
- this matches the existing Hydra-side scene snapshot cache
- progressive accumulation batches therefore reuse the same BVH instead of
  rebuilding acceleration every update

Current scope:

- triangle meshes only
- one Embree geometry per translated mesh
- shading normal still comes from Shiro mesh data / geometric fallback
- no instances, motion blur, curves, or volumes yet

## Viewport image orientation note

The upside-down viewport output was not a transport bug.

The renderer had been generating primary rays with a top-left raster
convention, while the Hydra viewport path in practice expected the lower-left
orientation that matched the copied AOV buffer layout.

Current correction:

- `GenerateCameraRay()` now uses positive `filmY` against `camera.up`
- this aligns Shiro's raster convention with the viewport AOV presentation
  path

If the image ever appears inverted again after future refactors, inspect camera
ray generation first before changing path transport or light sampling.

## CUDA / OptiX note

Current outcome of the OptiX integration phase:

- source/build layout is now split more clearly into runtime, backend, and
  frontend targets
- CPU/Embree rendering moved behind a backend interface
- an OptiX backend module now exists and is linked into the project as an
  optional backend
- the OptiX backend uses:
  - CUDA driver API for device/context/stream creation
  - NVRTC for runtime compilation of `src/backend/optix/OptixPrograms.cu`
  - OptiX stubs for module/program/pipeline launch

Important design choice:

- build-time CUDA language enablement was intentionally avoided for the first
  pass
- instead, OptiX device code is compiled at runtime through NVRTC
- this keeps the top-level project in `LANGUAGES CXX` while still producing a
  real CUDA/OptiX execution path
- this also avoids making the whole build dependent on `nvcc` being on `PATH`

Current scope of the GPU backend:

- triangle GAS build for the current scene snapshot
- closest-hit shading for base / specular / coat / transmission / emission
- directional-light shadows plus dome-light HDRI sampling and environment-gradient fallback
- beauty / albedo / normal / depth AOV output
- Hydra material translation for `UsdPreviewSurface`, `standard_surface`, and `openpbr_surface`
- no hybrid scheduler yet

Why this scope is still useful:

- it proves the project can discover CUDA/OptiX dependencies cleanly
- it proves Shiro can compile GPU code at runtime and launch an OptiX pipeline
- it establishes the backend boundary needed for dome-light and XPU work
- it avoids destabilizing the current Hydra/CPU renderer while the GPU backend
  is still immature

Runtime finding on the local AlmaLinux machine:

- `nvidia-smi` succeeds and reports driver `580.76.05` on `NVIDIA GeForce RTX 4080`
- the distro-provided OptiX 9.1 headers are newer than the driver-side OptiX runtime ABI exposed by `libnvoptix.so.1`
- the project now vendors OptiX 9.0 headers under `external/optix-sdk-9.0.0` and points `SHIRO_OPTIX_ROOT` there by default
- `tools/shiro_optix_probe` now succeeds, including NVRTC compile, OptiX module creation, pipeline creation, and a geometry hit on the GPU

Conclusion:

- build integration is successful
- runtime GPU execution is working on the local machine when the OptiX headers are kept ABI-compatible with the driver runtime
- the next renderer steps are quality work: hybrid scheduling, wider texture/material network coverage, and MIS/denoising rather than basic bring-up

## References

- OpenUSD API docs: https://openusd.org/dev/api/
- OpenUSD Hydra overview: https://openusd.org/release/api/_page__hydra__getting__started__guide.html
- OpenUSD/HDK material schema tokens: `/opt/hfs20.5.410/toolkit/include/pxr/imaging/hd/material.h`
- OpenUSD/HDK light tokens: `/opt/hfs20.5.410/toolkit/include/pxr/imaging/hd/light.h`
- OpenUSD/HDK scene delegate interfaces: `/opt/hfs20.5.410/toolkit/include/pxr/imaging/hd/sceneDelegate.h`
- SideFX HDK CMake package docs: https://www.sidefx.com/docs/hdk/_h_d_k__intro__compiling.html
- SideFX USD/Hydra integration notes: https://www.sidefx.com/docs/hdk/_h_d_k__u_s_d_hydra.html
- SideFX Karma render settings docs: https://www.sidefx.com/docs/houdini/solaris/kug/settings.html
- MoonRay documentation: https://docs.openmoonray.org/
- MoonRay render settings / ray depth docs: https://docs.openmoonray.org/user-reference/how-to-guides/render-settings/
- MaterialX specification and docs: https://materialx.org/
- MaterialX standard surface docs: https://kwokcb.github.io/MaterialX_Learn/documents/definitions/standard_surface.html
- OpenPBR specification: https://academysoftwarefoundation.github.io/OpenPBR/
- OpenPBR surface specification draft: https://academysoftwarefoundation.github.io/OpenPBR/index.html
- OpenQMC repository and docs: https://github.com/MomentsInGraphics/openqmc
- Redshift USD/Solaris render settings docs: https://help.maxon.net/r3d/houdini/en-us/Content/html/Houdini%2BSolaris%2BRender%2BSettings.html
- VFX Reference Platform: https://vfxplatform.com/

## CUDA / XPU reality check

The current Shiro implementation should be described precisely as:

- CPU path tracer
- Embree-backed CPU ray traversal
- Hydra render delegate integration layer
- OptiX/CUDA-backed GPU path for triangle scenes

Evidence from the current codebase:

- `Renderer.h` exposes `BackendKind::{Cpu,Gpu,Hybrid}`
- `RenderParam.cpp` defaults interactive settings to `BackendKind::Hybrid`
- `Renderer.cpp` dispatches through backend modules instead of hardwiring a
  row-threaded CPU loop
- the top-level `CMakeLists.txt` declares only `LANGUAGES CXX`
- `src/backend/optix/OptixBackend.cpp` creates the CUDA/OptiX runtime path
- `src/backend/optix/OptixPrograms.cu` is compiled at runtime through NVRTC

Important nuance about OpenQMC:

- `external/openqmc` contains CUDA-aware code and its own CMake CUDA handling
- GPU rendering is now real, but that does not yet make Shiro a full XPU
  renderer
- the production reference path remains CPU/Embree while the GPU backend grows
  broader scene, texture, and scheduling coverage

Practical conclusion:

- current work should continue treating CPU/Embree as the production reference
  backend
- the honest next target is an OptiX/CUDA backend that can later participate in
  a
  real XPU scheduler with the CPU path
