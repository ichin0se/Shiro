#include "shiro/hydra/Material.h"

#if SHIRO_WITH_USD

#include <pxr/imaging/hd/changeTracker.h>

PXR_NAMESPACE_OPEN_SCOPE

HdShiroMaterial::HdShiroMaterial(const SdfPath& id)
    : HdMaterial(id) {
}

void HdShiroMaterial::Sync(HdSceneDelegate* sceneDelegate, HdRenderParam* renderParam, HdDirtyBits* dirtyBits) {
    (void)sceneDelegate;
    (void)renderParam;
    *dirtyBits = HdChangeTracker::Clean;
}

void HdShiroMaterial::Finalize(HdRenderParam* renderParam) {
    (void)renderParam;
}

HdDirtyBits HdShiroMaterial::GetInitialDirtyBitsMask() const {
    return HdChangeTracker::AllDirty;
}

PXR_NAMESPACE_CLOSE_SCOPE

#endif
