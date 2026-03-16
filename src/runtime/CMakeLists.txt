add_library(shiro_runtime
    ../render/EnvironmentMap.cpp
    ../render/FrameBuffer.cpp
    ../render/OpenQmcSampler.cpp
)
set_target_properties(shiro_runtime PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_link_libraries(shiro_runtime PUBLIC shiro_project_options)

if(MSVC AND SHIRO_HAVE_OPENQMC)
    set_source_files_properties(
        ../render/OpenQmcSampler.cpp
        PROPERTIES
            COMPILE_OPTIONS "/wd4333"
    )
endif()

if(SHIRO_HAVE_OPENQMC)
    target_link_libraries(shiro_runtime PUBLIC OpenQMC::OpenQMC)
endif()

if(SHIRO_HAVE_OIIO AND SHIRO_TEXTURE_LIBRARIES)
    target_link_libraries(shiro_runtime PUBLIC ${SHIRO_TEXTURE_LIBRARIES})
endif()
