if(NOT SHIRO_ENABLE_USD)
    return()
endif()

set(SHIRO_HD_PLUGIN_LIBRARY_NAME "hdShiro${CMAKE_SHARED_LIBRARY_SUFFIX}")
set(SHIRO_HD_PLUGIN_INFO_FILE "${CMAKE_BINARY_DIR}/generated/hdShiro/plugInfo.json")
file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/generated/hdShiro")
configure_file(
    "${CMAKE_SOURCE_DIR}/plugins/hdShiro/plugInfo.json"
    "${SHIRO_HD_PLUGIN_INFO_FILE}"
    @ONLY
)

add_library(hdShiro SHARED
    ../hydra/Tokens.cpp
    ../hydra/SceneBridge.cpp
    ../hydra/RenderParam.cpp
    ../hydra/RenderBuffer.cpp
    ../hydra/RenderPass.cpp
    ../hydra/Mesh.cpp
    ../hydra/Camera.cpp
    ../hydra/Light.cpp
    ../hydra/Material.cpp
    ../hydra/RenderDelegate.cpp
    ../hydra/RendererPlugin.cpp
)
set_target_properties(hdShiro PROPERTIES PREFIX "" POSITION_INDEPENDENT_CODE ON)
target_link_libraries(hdShiro
    PRIVATE
        shiro_project_options
        shiro_core
        ${SHIRO_USD_COMPILE_TARGETS}
        ${SHIRO_USD_LIBRARIES}
)
target_compile_definitions(hdShiro PRIVATE
    SHIRO_HD_BUILDING_LIBRARY=1
    _SILENCE_CXX20_OLD_SHARED_PTR_ATOMIC_SUPPORT_DEPRECATION_WARNING=1
)

set(_shiro_runtime_rpath "")
if(SHIRO_USD_PROVIDER STREQUAL "houdini" AND SHIRO_HOUDINI_ROOT_RESOLVED)
    list(APPEND _shiro_runtime_rpath "${SHIRO_HOUDINI_ROOT_RESOLVED}/dsolib")
endif()
if(SHIRO_RUNTIME_LIBRARY_DIRS)
    list(APPEND _shiro_runtime_rpath ${SHIRO_RUNTIME_LIBRARY_DIRS})
endif()
if(_shiro_runtime_rpath)
    list(REMOVE_DUPLICATES _shiro_runtime_rpath)
    list(JOIN _shiro_runtime_rpath ";" _shiro_runtime_rpath_joined)
    set_target_properties(hdShiro PROPERTIES
        BUILD_RPATH "${_shiro_runtime_rpath_joined}"
        INSTALL_RPATH "${_shiro_runtime_rpath_joined}"
    )
endif()

if(SHIRO_USD_PLUGIN_RESOURCE_DIR)
    set(SHIRO_STAGE_USD_DIR "${SHIRO_PLUGIN_STAGE_DIR}/dso/usd")
    set(SHIRO_STAGE_USD_PLUGIN_DIR "${SHIRO_PLUGIN_STAGE_DIR}/dso/usd_plugins")
    add_custom_command(TARGET hdShiro POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "${SHIRO_STAGE_USD_DIR}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${SHIRO_STAGE_USD_PLUGIN_DIR}/hdShiro/resources"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:hdShiro>"
            "${SHIRO_STAGE_USD_DIR}/$<TARGET_FILE_NAME:hdShiro>"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_SOURCE_DIR}/plugins/usd_plugins/plugInfo.json"
            "${SHIRO_STAGE_USD_PLUGIN_DIR}/plugInfo.json"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${SHIRO_HD_PLUGIN_INFO_FILE}"
            "${SHIRO_STAGE_USD_PLUGIN_DIR}/hdShiro/resources/plugInfo.json"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_SOURCE_DIR}/plugins/UsdRenderers.json"
            "${SHIRO_PLUGIN_STAGE_DIR}/UsdRenderers.json"
    )

    install(TARGETS hdShiro
        LIBRARY DESTINATION "dso/usd"
        RUNTIME DESTINATION "dso/usd"
    )
    install(FILES
        "${CMAKE_SOURCE_DIR}/plugins/usd_plugins/plugInfo.json"
        DESTINATION "dso/usd_plugins"
    )
    install(FILES
        "${SHIRO_HD_PLUGIN_INFO_FILE}"
        DESTINATION "dso/usd_plugins/hdShiro/resources"
    )
    install(FILES
        "${CMAKE_SOURCE_DIR}/plugins/UsdRenderers.json"
        DESTINATION "."
    )
endif()
