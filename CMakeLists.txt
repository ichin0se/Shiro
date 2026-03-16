cmake_minimum_required(VERSION 3.24)

project(Shiro VERSION 0.1.0 LANGUAGES CXX)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

include(cmake/ShiroOptions.cmake)
include(cmake/ShiroDependencies.cmake)

add_library(shiro_project_options INTERFACE)
target_include_directories(shiro_project_options
    INTERFACE
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        ${SHIRO_EMBREE_INCLUDE_DIRS}
        ${SHIRO_TEXTURE_INCLUDE_DIRS}
        ${SHIRO_CUDA_INCLUDE_DIRS}
        ${SHIRO_OPTIX_INCLUDE_DIRS}
)
target_compile_features(shiro_project_options INTERFACE cxx_std_17)
target_compile_definitions(shiro_project_options
    INTERFACE
        SHIRO_HAVE_CUDA=$<BOOL:${SHIRO_HAVE_CUDA}>
        SHIRO_HAVE_EMBREE=$<BOOL:${SHIRO_HAVE_EMBREE}>
        SHIRO_HAVE_OIIO=$<BOOL:${SHIRO_HAVE_OIIO}>
        SHIRO_HAVE_OPENSUBDIV=$<BOOL:${SHIRO_HAVE_OPENSUBDIV}>
        SHIRO_HAVE_OPTIX=$<BOOL:${SHIRO_HAVE_OPTIX}>
        SHIRO_WITH_OPENQMC=$<BOOL:${SHIRO_HAVE_OPENQMC}>
        SHIRO_WITH_USD=$<BOOL:${SHIRO_ENABLE_USD}>
)

add_subdirectory(src)
add_subdirectory(tools)
