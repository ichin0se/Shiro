# Shiro Architecture

## Design goals

Shiro is split around the same fault lines that matter in a production renderer:

1. Hydra-facing scene ingestion must be isolated from transport and scheduling.
2. The transport core must be able to run on CPU, GPU, or both.
3. Sampling, acceleration, shading, and output need replaceable implementations.

## Modules

- `include/shiro/render`, `src/render`, and `src/runtime`
  - renderer facade, frame buffers, environment maps, sampling helpers, and backend-independent runtime types
- `include/shiro/backend` and `src/backend`
  - execution backends and scheduling boundaries
  - `cpu/` holds the Embree-backed reference path tracer
  - `optix/` holds the CUDA/OptiX GPU path
- `include/shiro/hydra` and `src/hydra`
  - Hydra plugin boundary, render delegate, render pass, render buffers, and scene translation
- `src/frontend`
  - frontend-specific build packaging for `hdShiro`
- `tools`
  - standalone validation utilities such as `shiro_optix_probe`
- `plugins/hdShiro`
  - USD plug metadata
- `docs`
  - research notes, dependency choices, and staged roadmap

## Current execution model

The first implementation uses a CPU path tracer as the reference backend. `Renderer` is now a thin facade that dispatches into backend modules so the following upgrades can slot in without changing the Hydra layer:

- CPU reference backend with wide BVH traversal.
- CUDA/OptiX backend for triangle traversal, dome-light HDRI sampling, and PBR path tracing.
- GPU wavefront backend for high-throughput path states.
- Hybrid/XPU scheduler that routes memory-heavy or divergence-heavy workloads to CPU while keeping coherent primary/secondary queues on GPU.

Current limitation:

- the OptiX backend still lacks a true hybrid scheduler and broader texture/material coverage beyond the currently translated surface parameters
- CPU/Embree remains the only backend that should be treated as feature-complete for Hydra rendering today

## Planned transport upgrades

- Two-level acceleration structure with mesh BLAS and instance TLAS.
- GPU-friendly wavefront queues and path compaction.
- ReSTIR-style many-light reuse for direct lighting where the bias/variance tradeoff is acceptable.
- Dedicated caustics path family such as MNEE or specular manifold sampling.
- Random-walk and Dwivedi-style sampling for heterogeneous and BSSRDF transport.
- MaterialX/OpenPBR translation and optional OSL closures.
