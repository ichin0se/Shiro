set(SHIRO_HAVE_OPENQMC OFF)
set(SHIRO_HAVE_EMBREE OFF)
set(SHIRO_HAVE_OIIO OFF)
set(SHIRO_HAVE_OPENSUBDIV OFF)
set(SHIRO_HAVE_CUDA OFF)
set(SHIRO_HAVE_OPTIX OFF)
set(SHIRO_EMBREE_PROVIDER "none")
set(SHIRO_OIIO_PROVIDER "none")
set(SHIRO_CUDA_PROVIDER "none")
set(SHIRO_OPTIX_PROVIDER "none")
set(SHIRO_USD_LIBRARIES "")
set(SHIRO_USD_COMPILE_TARGETS "")
set(SHIRO_USD_PLUGIN_RESOURCE_DIR OFF)
set(SHIRO_USD_PROVIDER "none")
set(SHIRO_HOUDINI_ROOT_RESOLVED "")
set(SHIRO_EMBREE_INCLUDE_DIRS "")
set(SHIRO_EMBREE_LIBRARIES "")
set(SHIRO_TEXTURE_INCLUDE_DIRS "")
set(SHIRO_TEXTURE_LIBRARIES "")
set(SHIRO_CUDA_INCLUDE_DIRS "")
set(SHIRO_CUDA_LIBRARIES "")
set(SHIRO_OPTIX_INCLUDE_DIRS "")
set(SHIRO_OPTIX_LIBRARIES "")
set(SHIRO_RUNTIME_LIBRARY_DIRS "")

if(SHIRO_ENABLE_OPENQMC)
    find_package(OpenQMC CONFIG QUIET)
    if(OpenQMC_FOUND)
        set(SHIRO_HAVE_OPENQMC ON)
    elseif(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/external/openqmc/CMakeLists.txt")
        set(OPENQMC_ARCH_TYPE Scalar CACHE STRING "" FORCE)
        set(OPENQMC_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
        set(OPENQMC_BUILD_TESTING OFF CACHE BOOL "" FORCE)
        set(OPENQMC_ENABLE_BINARY OFF CACHE BOOL "" FORCE)
        add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/external/openqmc" EXCLUDE_FROM_ALL)
        set(SHIRO_HAVE_OPENQMC ON)
    endif()
endif()

if(SHIRO_ENABLE_USD)
    set(_shiro_houdini_candidates "")
    if(SHIRO_HOUDINI_ROOT)
        list(APPEND _shiro_houdini_candidates "${SHIRO_HOUDINI_ROOT}")
    endif()
    if(DEFINED ENV{HFS} AND NOT "$ENV{HFS}" STREQUAL "")
        list(APPEND _shiro_houdini_candidates "$ENV{HFS}")
    endif()
    list(APPEND _shiro_houdini_candidates
        "/opt/hfs20.5.410"
        "/opt/hfs20.5"
        "/opt/hfs21.0.440"
        "/opt/hfs21.0"
        "/opt/hfs20.0.724"
        "/opt/hfs20.0"
    )

    foreach(_shiro_houdini_candidate IN LISTS _shiro_houdini_candidates)
        if(
            _shiro_houdini_candidate
            AND EXISTS "${_shiro_houdini_candidate}/toolkit/cmake/HoudiniConfig.cmake"
        )
            set(SHIRO_HOUDINI_ROOT_RESOLVED "${_shiro_houdini_candidate}")
            break()
        endif()
    endforeach()

    if(SHIRO_HOUDINI_ROOT_RESOLVED)
        list(PREPEND CMAKE_PREFIX_PATH "${SHIRO_HOUDINI_ROOT_RESOLVED}/toolkit/cmake")
        find_package(Houdini QUIET CONFIG)
    endif()

    if(Houdini_FOUND)
        add_library(shiro_usd_provider INTERFACE)
        target_include_directories(shiro_usd_provider SYSTEM INTERFACE
            $<TARGET_PROPERTY:Houdini,INTERFACE_INCLUDE_DIRECTORIES>
        )
        target_compile_definitions(shiro_usd_provider INTERFACE
            $<TARGET_PROPERTY:Houdini,INTERFACE_COMPILE_DEFINITIONS>
        )
        target_compile_options(shiro_usd_provider INTERFACE
            $<TARGET_PROPERTY:Houdini,INTERFACE_COMPILE_OPTIONS>
        )

        set(SHIRO_USD_PROVIDER "houdini")
        set(SHIRO_USD_PLUGIN_RESOURCE_DIR ON)
        set(SHIRO_USD_COMPILE_TARGETS shiro_usd_provider)
        set(SHIRO_USD_LIBRARIES
            Houdini::Dep::pxr_arch
            Houdini::Dep::osdCPU
            Houdini::Dep::pxr_gf
            Houdini::Dep::pxr_hd
            Houdini::Dep::pxr_hf
            Houdini::Dep::pxr_plug
            Houdini::Dep::pxr_pxOsd
            Houdini::Dep::pxr_sdf
            Houdini::Dep::pxr_tf
            Houdini::Dep::pxr_vt
            Houdini::Dep::pxr_work
        )
        set(SHIRO_HAVE_OPENSUBDIV ON)
    else()
        find_package(pxr CONFIG QUIET)
        if(NOT pxr_FOUND)
            message(WARNING "Neither Houdini nor pxr package was found. hdShiro will not be built.")
            set(SHIRO_ENABLE_USD OFF CACHE BOOL "" FORCE)
        else()
            set(SHIRO_USD_PROVIDER "pxr")
            set(SHIRO_USD_PLUGIN_RESOURCE_DIR ON)
            find_library(SHIRO_OSDCPU_LIBRARY NAMES osdCPU)
            find_library(SHIRO_PXOSD_LIBRARY NAMES pxOsd pxr_pxOsd)
            set(SHIRO_USD_LIBRARIES
                arch
                gf
                hd
                hf
                ${SHIRO_OSDCPU_LIBRARY}
                ${SHIRO_PXOSD_LIBRARY}
                plug
                sdf
                tf
                vt
                work
            )
            if(SHIRO_OSDCPU_LIBRARY AND SHIRO_PXOSD_LIBRARY)
                set(SHIRO_HAVE_OPENSUBDIV ON)
            endif()
        endif()
    endif()
endif()

if(SHIRO_ENABLE_EMBREE)
    set(_shiro_embree_include_hints "")
    set(_shiro_embree_library_hints "")

    if(SHIRO_EMBREE_ROOT)
        list(APPEND _shiro_embree_include_hints
            "${SHIRO_EMBREE_ROOT}/include"
            "${SHIRO_EMBREE_ROOT}/usr/include"
        )
        list(APPEND _shiro_embree_library_hints
            "${SHIRO_EMBREE_ROOT}/lib64"
            "${SHIRO_EMBREE_ROOT}/usr/lib64"
            "${SHIRO_EMBREE_ROOT}/lib"
            "${SHIRO_EMBREE_ROOT}/usr/lib"
        )
    endif()

    find_path(SHIRO_EMBREE_INCLUDE_DIR
        NAMES rtcore.h
        HINTS ${_shiro_embree_include_hints}
        PATH_SUFFIXES embree3
    )
    find_library(SHIRO_EMBREE_LIBRARY
        NAMES embree3
        HINTS ${_shiro_embree_library_hints}
    )

    if(SHIRO_EMBREE_INCLUDE_DIR AND SHIRO_EMBREE_LIBRARY)
        set(SHIRO_HAVE_EMBREE ON)
        set(SHIRO_EMBREE_INCLUDE_DIRS "${SHIRO_EMBREE_INCLUDE_DIR}")
        set(SHIRO_EMBREE_LIBRARIES "${SHIRO_EMBREE_LIBRARY}")
        get_filename_component(SHIRO_EMBREE_LIBRARY_DIR "${SHIRO_EMBREE_LIBRARY}" DIRECTORY)
        set(SHIRO_EMBREE_PROVIDER "manual")
        if(SHIRO_EMBREE_ROOT)
            set(SHIRO_EMBREE_PROVIDER "manual-root")
        else()
            set(SHIRO_EMBREE_PROVIDER "system")
        endif()
        if(NOT SHIRO_EMBREE_LIBRARY_DIR MATCHES "^/usr/lib" AND NOT SHIRO_EMBREE_LIBRARY_DIR MATCHES "^/lib")
            list(APPEND SHIRO_RUNTIME_LIBRARY_DIRS "${SHIRO_EMBREE_LIBRARY_DIR}")
        endif()
    endif()
endif()

if(SHIRO_HAVE_EMBREE)
    message(STATUS "Shiro Embree provider: ${SHIRO_EMBREE_PROVIDER}")
elseif(SHIRO_ENABLE_EMBREE)
    if(SHIRO_REQUIRE_EMBREE)
        message(FATAL_ERROR
            "Embree support is required but no provider was found. "
            "Install embree-devel or point SHIRO_EMBREE_ROOT at an Embree installation."
        )
    endif()
    message(STATUS "Shiro Embree provider: none (falling back to brute-force intersections)")
endif()

if(SHIRO_ENABLE_OIIO)
    find_package(OpenImageIO QUIET)
    if(OpenImageIO_FOUND)
        set(SHIRO_HAVE_OIIO ON)
        set(SHIRO_OIIO_PROVIDER "standalone")
        if(TARGET OpenImageIO::OpenImageIO)
            list(APPEND SHIRO_TEXTURE_LIBRARIES OpenImageIO::OpenImageIO)
        endif()
        if(TARGET OpenImageIO::OpenImageIO_Util)
            list(APPEND SHIRO_TEXTURE_LIBRARIES OpenImageIO::OpenImageIO_Util)
        endif()
    elseif(SHIRO_ALLOW_HOUDINI_OIIO_FALLBACK AND SHIRO_HOUDINI_ROOT_RESOLVED)
        find_path(SHIRO_OIIO_INCLUDE_DIR
            NAMES OpenImageIO/imageio.h
            HINTS "${SHIRO_HOUDINI_ROOT_RESOLVED}/toolkit/include"
            NO_DEFAULT_PATH
        )
        find_library(SHIRO_OIIO_LIBRARY
            NAMES OpenImageIO_sidefx
            HINTS "${SHIRO_HOUDINI_ROOT_RESOLVED}/dsolib"
            NO_DEFAULT_PATH
        )
        find_library(SHIRO_OIIO_UTIL_LIBRARY
            NAMES OpenImageIO_Util_sidefx
            HINTS "${SHIRO_HOUDINI_ROOT_RESOLVED}/dsolib"
            NO_DEFAULT_PATH
        )

        if(SHIRO_OIIO_INCLUDE_DIR AND SHIRO_OIIO_LIBRARY AND SHIRO_OIIO_UTIL_LIBRARY)
            set(SHIRO_HAVE_OIIO ON)
            set(SHIRO_OIIO_PROVIDER "houdini")
            set(SHIRO_TEXTURE_INCLUDE_DIRS "${SHIRO_OIIO_INCLUDE_DIR}")
            set(SHIRO_TEXTURE_LIBRARIES "${SHIRO_OIIO_LIBRARY}" "${SHIRO_OIIO_UTIL_LIBRARY}")
        endif()
    endif()
endif()

if(SHIRO_HAVE_OIIO)
    message(STATUS "Shiro OIIO provider: ${SHIRO_OIIO_PROVIDER}")
    if(SHIRO_OIIO_PROVIDER STREQUAL "houdini")
        message(WARNING
            "Shiro is using Houdini-bundled OpenImageIO as a development fallback. "
            "For a DCC-independent renderer build, install standalone OIIO and configure "
            "CMake to find it, or set SHIRO_ALLOW_HOUDINI_OIIO_FALLBACK=OFF."
        )
    endif()
elseif(SHIRO_ENABLE_OIIO)
    if(SHIRO_REQUIRE_OIIO)
        message(FATAL_ERROR
            "OpenImageIO support is required but no provider was found. "
            "Install standalone OpenImageIO, point CMake at it, or disable SHIRO_REQUIRE_OIIO."
        )
    endif()
    message(STATUS "Shiro OIIO provider: none (HDRI dome textures disabled)")
endif()

if(SHIRO_ENABLE_CUDA)
    set(_shiro_cuda_prefix_hints "")
    if(SHIRO_CUDA_ROOT)
        list(APPEND _shiro_cuda_prefix_hints "${SHIRO_CUDA_ROOT}")
    endif()
    list(APPEND _shiro_cuda_prefix_hints
        "/usr/local/cuda"
        "/usr/local/cuda-13.0"
        "/usr/local/cuda-13"
        "/usr/local/cuda-12.8"
        "/usr/local/cuda-11.8"
    )

    foreach(_shiro_cuda_prefix IN LISTS _shiro_cuda_prefix_hints)
        if(_shiro_cuda_prefix AND EXISTS "${_shiro_cuda_prefix}/targets/x86_64-linux/include/cuda_runtime_api.h")
            list(PREPEND CMAKE_PREFIX_PATH "${_shiro_cuda_prefix}")
            break()
        endif()
    endforeach()

    find_package(CUDAToolkit QUIET)
    if(CUDAToolkit_FOUND)
        set(SHIRO_HAVE_CUDA ON)
        set(SHIRO_CUDA_PROVIDER "toolkit")
        set(SHIRO_CUDA_INCLUDE_DIRS ${CUDAToolkit_INCLUDE_DIRS})
        list(APPEND SHIRO_CUDA_LIBRARIES CUDA::cuda_driver CUDA::nvrtc)
        if(TARGET CUDA::cudart)
            list(APPEND SHIRO_CUDA_LIBRARIES CUDA::cudart)
        endif()
        if(CUDAToolkit_LIBRARY_ROOT)
            if(NOT CUDAToolkit_LIBRARY_ROOT MATCHES "^/usr/lib" AND NOT CUDAToolkit_LIBRARY_ROOT MATCHES "^/lib")
                list(APPEND SHIRO_RUNTIME_LIBRARY_DIRS "${CUDAToolkit_LIBRARY_ROOT}")
            endif()
        endif()
    endif()
endif()

if(SHIRO_HAVE_CUDA)
    message(STATUS "Shiro CUDA provider: ${SHIRO_CUDA_PROVIDER}")
elseif(SHIRO_ENABLE_CUDA)
    if(SHIRO_REQUIRE_CUDA)
        message(FATAL_ERROR
            "CUDA support is required but CUDAToolkit could not be resolved. "
            "Install cuda-toolkit or point SHIRO_CUDA_ROOT at the toolkit root."
        )
    endif()
    message(STATUS "Shiro CUDA provider: none")
endif()

if(SHIRO_ENABLE_OPTIX)
    set(_shiro_optix_include_hints "")
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/external/optix-sdk-9.0.0/include/optix.h")
        list(APPEND _shiro_optix_include_hints
            "${CMAKE_CURRENT_SOURCE_DIR}/external/optix-sdk-9.0.0/include"
            "${CMAKE_CURRENT_SOURCE_DIR}/external/optix-sdk-9.0.0"
        )
    endif()
    if(SHIRO_OPTIX_ROOT)
        list(APPEND _shiro_optix_include_hints
            "${SHIRO_OPTIX_ROOT}/include"
            "${SHIRO_OPTIX_ROOT}"
        )
    endif()
    list(APPEND _shiro_optix_include_hints
        "/usr/include"
        "/usr/local/include"
    )

    find_path(SHIRO_OPTIX_INCLUDE_DIR
        NAMES optix.h
        HINTS ${_shiro_optix_include_hints}
        PATH_SUFFIXES optix
    )

    if(SHIRO_HAVE_CUDA AND SHIRO_OPTIX_INCLUDE_DIR)
        set(SHIRO_HAVE_OPTIX ON)
        set(SHIRO_OPTIX_PROVIDER "sdk")
        set(SHIRO_OPTIX_INCLUDE_DIRS "${SHIRO_OPTIX_INCLUDE_DIR}")
    endif()
endif()

if(SHIRO_HAVE_OPTIX)
    message(STATUS "Shiro OptiX provider: ${SHIRO_OPTIX_PROVIDER}")
elseif(SHIRO_ENABLE_OPTIX)
    if(SHIRO_REQUIRE_OPTIX)
        message(FATAL_ERROR
            "OptiX support is required but the SDK headers could not be resolved. "
            "Install nvidia-sdk-optix or point SHIRO_OPTIX_ROOT at the SDK root."
        )
    endif()
    message(STATUS "Shiro OptiX provider: none")
endif()
