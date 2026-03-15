set(SHIRO_HAVE_OPENQMC OFF)
set(SHIRO_USD_LIBRARIES "")
set(SHIRO_USD_PLUGIN_RESOURCE_DIR OFF)

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
    find_package(pxr CONFIG QUIET)
    if(NOT pxr_FOUND)
        message(WARNING "pxr package was not found. hdShiro will not be built.")
        set(SHIRO_ENABLE_USD OFF CACHE BOOL "" FORCE)
    else()
        set(SHIRO_USD_PLUGIN_RESOURCE_DIR ON)
        set(SHIRO_USD_LIBRARIES
            arch
            gf
            hd
            hf
            plug
            sdf
            tf
            vt
            work
        )
    endif()
endif()
