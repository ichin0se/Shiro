#include "shiro/hydra/Mesh.h"

#if SHIRO_WITH_USD

#include "shiro/hydra/RenderParam.h"
#include "shiro/hydra/SceneBridge.h"

#include <pxr/imaging/hd/changeTracker.h>

PXR_NAMESPACE_OPEN_SCOPE

HdShiroMesh::HdShiroMesh(const SdfPath& id)
    : HdMesh(id) {
}

void HdShiroMesh::Sync(
    HdSceneDelegate* sceneDelegate,
    HdRenderParam* renderParam,
    HdDirtyBits* dirtyBits,
    const TfToken& reprToken) {
    (void)reprToken;

    auto* shiroRenderParam = static_cast<HdShiroRenderParam*>(renderParam);
    if (!shiroRenderParam) {
        *dirtyBits = HdChangeTracker::Clean;
        return;
    }

    if (const auto payload = HdShiroSceneBridge::ExtractMesh(sceneDelegate, GetId())) {
        shiroRenderParam->UpsertMesh(GetId().GetString(), payload->mesh, payload->material);
    }

    *dirtyBits = HdChangeTracker::Clean;
}

void HdShiroMesh::Finalize(HdRenderParam* renderParam) {
    if (auto* shiroRenderParam = static_cast<HdShiroRenderParam*>(renderParam)) {
        shiroRenderParam->RemoveMesh(GetId().GetString());
    }
}

HdDirtyBits HdShiroMesh::GetInitialDirtyBitsMask() const {
    return HdChangeTracker::AllDirty;
}

void HdShiroMesh::_InitRepr(const TfToken& reprToken, HdDirtyBits* dirtyBits) {
    (void)reprToken;
    *dirtyBits |= HdChangeTracker::AllDirty;
}

HdDirtyBits HdShiroMesh::_PropagateDirtyBits(HdDirtyBits dirtyBits) const {
    return dirtyBits;
}

PXR_NAMESPACE_CLOSE_SCOPE

#endif
