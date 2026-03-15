# Shiro Architecture

## Design goals

Shiro is split around the same fault lines that matter in a production renderer:

1. Hydra-facing scene ingestion must be isolated from transport and scheduling.
2. The transport core must be able to run on CPU, GPU, or both.
3. Sampling, acceleration, shading, and output need replaceable implementations.

## Modules

- `include/shiro/render` and `src/render`
  - Core renderer state, frame buffers, transport loop, scene format, and sampling.
- `include/shiro/hydra` and `src/hydra`
  - Hydra plugin boundary, render delegate, render pass, render buffers, and scene translation.
- `plugins/hdShiro`
  - USD plug metadata.
- `docs`
  - Research notes, dependency choices, and staged roadmap.

## Current execution model

The first implementation uses a CPU path tracer as the reference backend. The `Renderer` class already exposes a backend mode so the following upgrades can slot in without changing the Hydra layer:

- CPU reference backend with wide BVH traversal.
- GPU wavefront backend for high-throughput path states.
- Hybrid/XPU scheduler that routes memory-heavy or divergence-heavy workloads to CPU while keeping coherent primary/secondary queues on GPU.

## Planned transport upgrades

- Two-level acceleration structure with mesh BLAS and instance TLAS.
- GPU-friendly wavefront queues and path compaction.
- ReSTIR-style many-light reuse for direct lighting where the bias/variance tradeoff is acceptable.
- Dedicated caustics path family such as MNEE or specular manifold sampling.
- Random-walk and Dwivedi-style sampling for heterogeneous and BSSRDF transport.
- MaterialX/OpenPBR translation and optional OSL closures.
