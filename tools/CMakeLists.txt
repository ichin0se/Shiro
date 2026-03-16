add_executable(shiro_optix_probe
    optix_probe.cpp
)
target_link_libraries(shiro_optix_probe
    PRIVATE
        shiro_project_options
        shiro_core
)
