# Renderer Roadmap

## Immediate next work

1. Replace brute-force triangle intersection with Embree BLAS/TLAS on CPU.
2. Add explicit light sampling and MIS.
3. Expand USD material-network coverage from the current `UsdPreviewSurface` / `standard_surface` / `openpbr_surface` parameter subset to texture-driven graphs.
4. Add volume and SSS path families.
5. Add a true hybrid scheduler on top of the OptiX backend.

## Production readiness checklist

- Deterministic sampling across CPU and GPU.
- Progressive and bucket rendering.
- Resume-safe checkpointing.
- Out-of-core geometry and texture residency.
- Crash-safe scene update and render cancellation.
- Render statistics, profiling, and memory accounting.
