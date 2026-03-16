# Build Notes

## Primary Linux target

Validated on this machine:

- AlmaLinux 9
- CUDA toolkit 13.0 installed under `/usr/local/cuda-13.0`
- NVIDIA driver `580.76.05`
- bundled OptiX SDK 9.0 headers under `external/optix-sdk-9.0.0`
- Houdini 20.5.410 in `/opt/hfs20.5.410`
- GCC 11.5.0
- CMake 3.26.5
- Ninja 1.13.0

Configure:

```bash
cmake --fresh --preset linux-houdini20.5
```

Build:

```bash
cmake --build --preset linux-houdini20.5
```

CUDA/OptiX configure:

```bash
cmake --fresh --preset linux-houdini20.5-cuda
```

CUDA/OptiX build:

```bash
cmake --build --preset linux-houdini20.5-cuda
```

Install:

```bash
cmake --install build-linux-houdini20.5
```

Staged plugin layout after build:

- `build-linux-houdini20.5/stage/dso/usd/hdShiro.so`
- `build-linux-houdini20.5/stage/dso/usd_plugins/plugInfo.json`
- `build-linux-houdini20.5/stage/dso/usd_plugins/hdShiro/resources/plugInfo.json`
- `build-linux-houdini20.5/stage/UsdRenderers.json`
- `build-linux-houdini20.5-cuda/tools/shiro_optix_probe`

Runtime environment:

```bash
export HOUDINI_PATH="$PWD/build-linux-houdini20.5/stage;&"
export PXR_PLUGINPATH_NAME="$PWD/build-linux-houdini20.5/stage/dso/usd_plugins"
```

Notes:

- The Houdini SDK path can be overridden with `-DSHIRO_HOUDINI_ROOT=/path/to/hfs`.
- The build prefers the SideFX USD/HDK toolchain when Houdini is available, and falls back to a standalone `pxr` package otherwise.
- Embree is now a required CPU acceleration dependency by default.
- CUDA toolkit is auto-detected from standard install roots such as `/usr/local/cuda`.
- OptiX defaults to the vendored 9.0 headers so the build stays aligned with the local `libnvoptix.so.1` ABI exposed by driver `580.76.05`.
- For a one-shot local setup helper, see `tools/install_cuda_almalinux9.sh`.
- On AlmaLinux 9 the expected package is `embree-devel`:

```bash
sudo dnf install -y embree-devel
```

- If Embree is not installed system-wide, point CMake at an extracted/package root:

```bash
cmake --fresh --preset linux-houdini20.5 \
  -DSHIRO_EMBREE_ROOT=/path/to/embree-root
```

- `SHIRO_REQUIRE_EMBREE=ON` is the default. Configure fails if Embree cannot be resolved.
- To require GPU dependencies at configure time:

```bash
cmake --fresh --preset linux-houdini20.5-cuda \
  -DSHIRO_REQUIRE_CUDA=ON \
  -DSHIRO_REQUIRE_OPTIX=ON
```

- HDRI / dome texture loading prefers a standalone OpenImageIO installation.
- Houdini-bundled OpenImageIO is only used as a fallback when `SHIRO_ALLOW_HOUDINI_OIIO_FALLBACK=ON`.
- If HDRI support must be guaranteed, add `-DSHIRO_REQUIRE_OIIO=ON`.
- Because Houdini ships its own namespaced Embree headers under `toolkit/include/embree3`, Shiro resolves the standalone Embree include directory as `.../embree3` and includes `<rtcore.h>` directly to avoid picking SideFX's private wrapper by accident.
- For a DCC-independent renderer build, point CMake at a standalone OIIO and disable the fallback:

```bash
cmake --fresh --preset linux-houdini20.5 \
  -DOpenImageIO_DIR=/path/to/oiio/lib/cmake/OpenImageIO \
  -DSHIRO_REQUIRE_OIIO=ON \
  -DSHIRO_ALLOW_HOUDINI_OIIO_FALLBACK=OFF
```

- `shiro_core` is built as PIC so `hdShiro.so` links cleanly on Linux.
- The current OptiX backend can render simple triangle scenes on the GPU with directional lights, dome-light HDRIs, `standard_surface`-style material response, and beauty/albedo/normal/depth AOVs.
- `tools/shiro_optix_probe` validates whether the host can actually create a CUDA/OptiX runtime path:

```bash
./build-linux-houdini20.5-cuda/tools/shiro_optix_probe
```

- On the current machine, `nvidia-smi` now succeeds and reports:
  - `Driver Version: 580.76.05`
  - `CUDA Version: 13.0`
  - `NVIDIA GeForce RTX 4080`
- On this machine as of 2026-03-16, `tools/shiro_optix_probe` succeeds after switching the project to the vendored OptiX 9.0 headers.
- If you override `SHIRO_OPTIX_ROOT`, keep it aligned with the driver-side runtime ABI. With driver `580.76.05`, OptiX 9.1 headers will fail at runtime with `OPTIX_ERROR_UNSUPPORTED_ABI_VERSION`.

## Windows path

Validated on the earlier development setup:

- Visual Studio Community 2022 17.14.28
- MSVC 19.44.35224
- Windows SDK 10.0.26100.0
- CMake 4.2.3
- Git 2.53.0.windows.2

Configure:

```powershell
& "C:\Program Files\CMake\bin\cmake.exe" --fresh --preset vs2022-x64
```

Build:

```powershell
& "C:\Program Files\CMake\bin\cmake.exe" --build --preset vs2022-debug
& "C:\Program Files\CMake\bin\cmake.exe" --build --preset vs2022-release
```

Notes:

- `vcpkg` installs OpenUSD into the build-local manifest directory during `cmake --fresh --preset vs2022-x64`.
- OpenQMC is wired through the bundled source tree and falls back deterministically if it is disabled.
