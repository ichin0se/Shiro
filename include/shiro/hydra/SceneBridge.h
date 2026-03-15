#pragma once

#include "shiro/core/Config.h"

#if SHIRO_WITH_USD

#include <memory>
#include <optional>

#include <pxr/usd/sdf/path.h>

#include "shiro/render/Renderer.h"

PXR_NAMESPACE_OPEN_SCOPE

class HdRenderPassState;
using HdRenderPassStateSharedPtr = std::shared_ptr<HdRenderPassState>;
class HdSceneDelegate;

struct HdShiroMeshPayload {
    shiro::render::TriangleMesh mesh;
    shiro::render::PbrMaterial material;
};

class HdShiroSceneBridge {
public:
    static std::optional<HdShiroMeshPayload> ExtractMesh(HdSceneDelegate* sceneDelegate, const SdfPath& id);
    static shiro::render::Camera BuildCamera(const HdRenderPassStateSharedPtr& renderPassState);
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif
