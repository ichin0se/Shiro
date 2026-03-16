#include "shiro/hydra/Mesh.h"

#if SHIRO_WITH_USD

#include "shiro/hydra/RenderParam.h"
#include "shiro/hydra/SceneBridge.h"

#include <pxr/imaging/hd/changeTracker.h>
#include <pxr/imaging/hd/sceneDelegate.h>

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

    const HdDirtyBits bits = *dirtyBits;
    const bool visibilityDirty = (bits & HdChangeTracker::DirtyVisibility) != 0;
    const bool meshDirty =
        visibilityDirty
        || (bits & HdChangeTracker::DirtyDisplayStyle) != 0
        || (bits & HdChangeTracker::DirtyPoints) != 0
        || (bits & HdChangeTracker::DirtyTopology) != 0
        || (bits & HdChangeTracker::DirtyTransform) != 0
        || (bits & HdChangeTracker::DirtyPrimvar) != 0
        || (bits & HdChangeTracker::DirtySubdivTags) != 0;
    const bool materialDirty = (bits & HdChangeTracker::DirtyMaterialId) != 0;
    const uint32_t maxSubdivLevel = shiroRenderParam->GetMaxSubdivLevel();

    if (visibilityDirty && sceneDelegate && !sceneDelegate->GetVisible(GetId())) {
        cachedPayload_.reset();
        cachedMaterialId_.clear();
        cachedMaxSubdivLevel_ = std::numeric_limits<uint32_t>::max();
        shiroRenderParam->RemoveMesh(GetId().GetString());
        *dirtyBits = HdChangeTracker::Clean;
        return;
    }

    if (meshDirty || !cachedPayload_ || cachedMaxSubdivLevel_ != maxSubdivLevel) {
        cachedPayload_ = HdShiroSceneBridge::ExtractMesh(sceneDelegate, GetId(), maxSubdivLevel);
        if (!cachedPayload_) {
            cachedMaterialId_.clear();
            cachedMaxSubdivLevel_ = std::numeric_limits<uint32_t>::max();
            shiroRenderParam->RemoveMesh(GetId().GetString());
            *dirtyBits = HdChangeTracker::Clean;
            return;
        }
        cachedMaxSubdivLevel_ = maxSubdivLevel;
    }

    if (meshDirty || materialDirty) {
        const SdfPath materialId = sceneDelegate ? sceneDelegate->GetMaterialId(GetId()) : SdfPath();
        cachedMaterialId_ = materialId.IsEmpty() ? std::string() : materialId.GetString();
        shiroRenderParam->UpsertMesh(
            GetId().GetString(),
            cachedPayload_->mesh,
            cachedPayload_->fallbackMaterial,
            cachedMaterialId_);
    }

    *dirtyBits = HdChangeTracker::Clean;
}

void HdShiroMesh::Finalize(HdRenderParam* renderParam) {
    if (auto* shiroRenderParam = static_cast<HdShiroRenderParam*>(renderParam)) {
        cachedPayload_.reset();
        cachedMaterialId_.clear();
        cachedMaxSubdivLevel_ = std::numeric_limits<uint32_t>::max();
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
