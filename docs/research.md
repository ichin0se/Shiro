# Research Notes

## External interfaces

- Hydra render delegates own Hydra primitive creation, render-pass creation, and resource commit boundaries. Shiro follows the same separation: `RenderDelegate` creates primitives, `RenderPass` triggers frame execution, and `RenderBuffer` stores AOV output.
- MoonRay is a good reference for production separation between scene ingestion, render prep, and rendering backends; Shiro mirrors that by keeping Hydra, scene translation, and transport isolated.
- OpenQMC is a strong fit for Shiro because it is header-only, supports CPU and GPU use cases, and is explicitly designed for domain branching in rendering workloads.

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

## References

- OpenUSD API docs: https://openusd.org/dev/api/
- MoonRay documentation: https://docs.openmoonray.org/
- VFX Reference Platform: https://vfxplatform.com/
