if(NOT SHIRO_ENABLE_USD)
    return()
endif()

set(SHIRO_HD_PLUGIN_LIBRARY_NAME "hdShiro${CMAKE_SHARED_LIBRARY_SUFFIX}")
set(SHIRO_HD_PLUGIN_INFO_FILE "${CMAKE_BINARY_DIR}/generated/hdShiro/plugInfo.json")
set(SHIRO_HOUDINI_OTL_GENERATOR "${CMAKE_SOURCE_DIR}/plugins/houdini/otls/generate_shiro_lops.py")
set(SHIRO_HOUDINI_OTL_LIBRARY "${CMAKE_BINARY_DIR}/generated/houdini_otls/ShiroLops.hda")
file(GLOB SHIRO_HOUDINI_SOHO_PARAMETER_FILES
    "${CMAKE_SOURCE_DIR}/plugins/houdini/soho/parameters/*.ds")
file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/generated/hdShiro")
file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/generated/houdini_otls")
configure_file(
    "${CMAKE_SOURCE_DIR}/plugins/hdShiro/plugInfo.json"
    "${SHIRO_HD_PLUGIN_INFO_FILE}"
    @ONLY
)

if(SHIRO_USD_PROVIDER STREQUAL "houdini" AND SHIRO_HOUDINI_ROOT_RESOLVED)
    add_custom_command(
        OUTPUT "${SHIRO_HOUDINI_OTL_LIBRARY}"
        COMMAND "${CMAKE_COMMAND}" -E env
            "PYTHONNOUSERSITE=1"
            "${SHIRO_HOUDINI_ROOT_RESOLVED}/bin/hython"
            "${SHIRO_HOUDINI_OTL_GENERATOR}"
            --output "${SHIRO_HOUDINI_OTL_LIBRARY}"
        DEPENDS
            "${SHIRO_HOUDINI_OTL_GENERATOR}"
            ${SHIRO_HOUDINI_SOHO_PARAMETER_FILES}
        VERBATIM
    )
    add_custom_target(shiro_houdini_otls DEPENDS "${SHIRO_HOUDINI_OTL_LIBRARY}")
endif()

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
    set(SHIRO_STAGE_SOHO_PARAMETER_DIR "${SHIRO_PLUGIN_STAGE_DIR}/soho/parameters")
    set(SHIRO_STAGE_OTL_DIR "${SHIRO_PLUGIN_STAGE_DIR}/otls")
    if(TARGET shiro_houdini_otls)
        add_dependencies(hdShiro shiro_houdini_otls)
    endif()
    add_custom_command(TARGET hdShiro POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "${SHIRO_STAGE_USD_DIR}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${SHIRO_STAGE_USD_PLUGIN_DIR}/hdShiro/resources"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${SHIRO_STAGE_SOHO_PARAMETER_DIR}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${SHIRO_STAGE_OTL_DIR}"
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
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${SHIRO_HOUDINI_SOHO_PARAMETER_FILES}
            "${SHIRO_STAGE_SOHO_PARAMETER_DIR}"
    )
    if(TARGET shiro_houdini_otls)
        add_custom_command(TARGET hdShiro POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${SHIRO_HOUDINI_OTL_LIBRARY}"
                "${SHIRO_STAGE_OTL_DIR}/ShiroLops.hda"
        )
    endif()

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
    install(FILES
        ${SHIRO_HOUDINI_SOHO_PARAMETER_FILES}
        DESTINATION "soho/parameters"
    )
    if(TARGET shiro_houdini_otls)
        install(FILES
            "${SHIRO_HOUDINI_OTL_LIBRARY}"
            DESTINATION "otls"
        )
    endif()
endif()
