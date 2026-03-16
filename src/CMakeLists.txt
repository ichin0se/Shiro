add_subdirectory(runtime)
add_subdirectory(backend)

add_library(shiro_core
    render/Renderer.cpp
)
set_target_properties(shiro_core PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_link_libraries(shiro_core
    PUBLIC
        shiro_project_options
        shiro_runtime
        shiro_backend_common
        shiro_backend_cpu
        shiro_backend_optix
)

add_subdirectory(frontend)
