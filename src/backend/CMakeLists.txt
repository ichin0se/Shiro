add_library(shiro_backend_common
    RenderBackend.cpp
)
set_target_properties(shiro_backend_common PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_link_libraries(shiro_backend_common PUBLIC shiro_project_options)

add_library(shiro_backend_cpu
    cpu/CpuPathTracer.cpp
)
set_target_properties(shiro_backend_cpu PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_link_libraries(shiro_backend_cpu
    PUBLIC
        shiro_project_options
        shiro_runtime
)

if(SHIRO_HAVE_EMBREE AND SHIRO_EMBREE_LIBRARIES)
    target_link_libraries(shiro_backend_cpu PUBLIC ${SHIRO_EMBREE_LIBRARIES})
endif()

add_library(shiro_backend_optix
    optix/OptixBackend.cpp
)
set_target_properties(shiro_backend_optix PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_link_libraries(shiro_backend_optix
    PUBLIC
        shiro_project_options
        shiro_runtime
)

if(SHIRO_HAVE_CUDA AND SHIRO_CUDA_LIBRARIES)
    target_link_libraries(shiro_backend_optix PUBLIC ${SHIRO_CUDA_LIBRARIES})
    if(UNIX AND NOT APPLE)
        target_link_libraries(shiro_backend_optix PUBLIC dl)
    endif()
endif()

set(_shiro_primary_cuda_include "")
if(SHIRO_CUDA_INCLUDE_DIRS)
    list(GET SHIRO_CUDA_INCLUDE_DIRS 0 _shiro_primary_cuda_include)
endif()

set(_shiro_primary_optix_include "")
if(SHIRO_OPTIX_INCLUDE_DIRS)
    list(GET SHIRO_OPTIX_INCLUDE_DIRS 0 _shiro_primary_optix_include)
endif()

target_compile_definitions(shiro_backend_optix
    PRIVATE
        SHIRO_PROJECT_INCLUDE_DIR="${CMAKE_SOURCE_DIR}/include"
        SHIRO_CUDA_INCLUDE_DIR="${_shiro_primary_cuda_include}"
        SHIRO_OPTIX_INCLUDE_DIR="${_shiro_primary_optix_include}"
        SHIRO_OPTIX_PROGRAM_SOURCE_PATH="${CMAKE_SOURCE_DIR}/src/backend/optix/OptixPrograms.cu"
)
