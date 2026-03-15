cmake_minimum_required(VERSION 3.24)

project(Shiro VERSION 0.1.0 LANGUAGES CXX)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

include(cmake/ShiroOptions.cmake)
include(cmake/ShiroDependencies.cmake)

add_library(shiro_core
    src/render/FrameBuffer.cpp
    src/render/OpenQmcSampler.cpp
    src/render/Renderer.cpp
)

target_include_directories(shiro_core
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

target_compile_features(shiro_core PUBLIC cxx_std_20)

target_compile_definitions(shiro_core
    PUBLIC
        SHIRO_WITH_USD=$<BOOL:${SHIRO_ENABLE_USD}>
        SHIRO_WITH_OPENQMC=$<BOOL:${SHIRO_HAVE_OPENQMC}>
)

if(MSVC AND SHIRO_HAVE_OPENQMC)
    set_source_files_properties(
        src/render/OpenQmcSampler.cpp
        PROPERTIES
            COMPILE_OPTIONS "/wd4333"
    )
endif()

if(SHIRO_HAVE_OPENQMC)
    target_link_libraries(shiro_core PUBLIC OpenQMC::OpenQMC)
endif()

if(SHIRO_ENABLE_USD)
    add_library(hdShiro SHARED
        src/hydra/Tokens.cpp
        src/hydra/SceneBridge.cpp
        src/hydra/RenderParam.cpp
        src/hydra/RenderBuffer.cpp
        src/hydra/RenderPass.cpp
        src/hydra/Mesh.cpp
        src/hydra/Camera.cpp
        src/hydra/Material.cpp
        src/hydra/RenderDelegate.cpp
        src/hydra/RendererPlugin.cpp
    )

    target_include_directories(hdShiro
        PUBLIC
            "${CMAKE_CURRENT_SOURCE_DIR}/include"
    )

    target_compile_features(hdShiro PUBLIC cxx_std_20)
    target_link_libraries(hdShiro PRIVATE shiro_core ${SHIRO_USD_LIBRARIES})
    target_compile_definitions(hdShiro PRIVATE
        SHIRO_HD_BUILDING_LIBRARY=1
        _SILENCE_CXX20_OLD_SHARED_PTR_ATOMIC_SUPPORT_DEPRECATION_WARNING=1
    )

    if(SHIRO_USD_PLUGIN_RESOURCE_DIR)
        add_custom_command(TARGET hdShiro POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:hdShiro>/resources"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${CMAKE_CURRENT_SOURCE_DIR}/plugins/hdShiro/plugInfo.json"
                "$<TARGET_FILE_DIR:hdShiro>/resources/plugInfo.json"
        )
    endif()
endif()
