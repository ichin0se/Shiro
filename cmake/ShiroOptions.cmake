set(_shiro_default_optix_root "${CMAKE_CURRENT_LIST_DIR}/../external/optix-sdk-9.0.0")
if(NOT EXISTS "${_shiro_default_optix_root}/include/optix.h")
    set(_shiro_default_optix_root "/usr")
endif()

option(SHIRO_ENABLE_USD "Build the Hydra render delegate." ON)
option(SHIRO_ENABLE_OPENQMC "Enable OpenQMC-backed sampling." ON)
option(SHIRO_ENABLE_EMBREE "Enable Embree-backed triangle acceleration." ON)
option(SHIRO_ENABLE_CUDA "Enable CUDA-backed GPU modules." ON)
option(
    SHIRO_REQUIRE_CUDA
    "Fail CMake configure if CUDA support cannot be resolved."
    OFF
)
option(SHIRO_ENABLE_OPTIX "Enable OptiX-backed GPU ray tracing modules." ON)
option(
    SHIRO_REQUIRE_OPTIX
    "Fail CMake configure if OptiX support cannot be resolved."
    OFF
)
option(
    SHIRO_REQUIRE_EMBREE
    "Fail CMake configure if Embree support cannot be resolved."
    ON
)
option(SHIRO_ENABLE_OIIO "Enable OpenImageIO-backed texture/image loading." ON)
option(
    SHIRO_REQUIRE_OIIO
    "Fail CMake configure if OpenImageIO support cannot be resolved."
    OFF
)
option(
    SHIRO_ALLOW_HOUDINI_OIIO_FALLBACK
    "Allow using Houdini-bundled OpenImageIO when standalone OIIO is unavailable."
    ON
)
set(
    SHIRO_CUDA_ROOT
    "/usr/local/cuda"
    CACHE PATH
    "Optional CUDA toolkit root used to resolve nvcc, headers, and libraries."
)
set(
    SHIRO_OPTIX_ROOT
    "${_shiro_default_optix_root}"
    CACHE PATH
    "Optional OptiX SDK root containing optix headers."
)
set(
    SHIRO_HOUDINI_ROOT
    "$ENV{HFS}"
    CACHE PATH
    "Path to a Houdini installation root used as the USD/HDK provider."
)
set(
    SHIRO_EMBREE_ROOT
    ""
    CACHE PATH
    "Optional root containing an Embree installation or extracted Embree package."
)
set(
    SHIRO_PLUGIN_STAGE_DIR
    "${CMAKE_BINARY_DIR}/stage"
    CACHE PATH
    "Staging root for the packaged Houdini/USD plugin layout."
)
