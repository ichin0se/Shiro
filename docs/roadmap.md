# Renderer Roadmap

## Immediate next work

1. Replace brute-force triangle intersection with Embree BLAS/TLAS on CPU.
2. Add explicit light sampling and MIS.
3. Translate USD material networks instead of relying on mesh display primvars.
4. Add volume and SSS path families.
5. Introduce OptiX-backed GPU traversal and a hybrid scheduler.

## Production readiness checklist

- Deterministic sampling across CPU and GPU.
- Progressive and bucket rendering.
- Resume-safe checkpointing.
- Out-of-core geometry and texture residency.
- Crash-safe scene update and render cancellation.
- Render statistics, profiling, and memory accounting.
