#pragma once

#include "shiro/core/Config.h"

#if SHIRO_WITH_USD

#include <memory>
#include <optional>

#include <pxr/base/vt/value.h>
#include <pxr/usd/sdf/path.h>

#include "shiro/render/Renderer.h"

PXR_NAMESPACE_OPEN_SCOPE

class HdRenderPassState;
using HdRenderPassStateSharedPtr = std::shared_ptr<HdRenderPassState>;
class HdSceneDelegate;

struct HdShiroMeshPayload {
    shiro::render::TriangleMesh mesh;
    shiro::render::PbrMaterial fallbackMaterial;
};

class HdShiroSceneBridge {
public:
    static std::optional<HdShiroMeshPayload> ExtractMesh(
        HdSceneDelegate* sceneDelegate,
        const SdfPath& id,
        uint32_t maxSubdivLevel);
    static std::optional<shiro::render::PbrMaterial> ExtractMaterial(const VtValue& materialResource);
    static std::optional<shiro::render::DomeLight> ExtractDomeLight(HdSceneDelegate* sceneDelegate, const SdfPath& id);
    static std::optional<shiro::render::DirectionalLight> ExtractDistantLight(HdSceneDelegate* sceneDelegate, const SdfPath& id);
    static shiro::render::Camera BuildCamera(
        const HdRenderPassStateSharedPtr& renderPassState,
        uint32_t imageWidth,
        uint32_t imageHeight);
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif
