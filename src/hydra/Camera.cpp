#include "shiro/hydra/Camera.h"

#if SHIRO_WITH_USD

#include <pxr/imaging/hd/changeTracker.h>

PXR_NAMESPACE_OPEN_SCOPE

HdShiroCamera::HdShiroCamera(const SdfPath& id)
    : HdCamera(id) {
}

void HdShiroCamera::Sync(HdSceneDelegate* sceneDelegate, HdRenderParam* renderParam, HdDirtyBits* dirtyBits) {
    HdCamera::Sync(sceneDelegate, renderParam, dirtyBits);
}

void HdShiroCamera::Finalize(HdRenderParam* renderParam) {
    (void)renderParam;
}

HdDirtyBits HdShiroCamera::GetInitialDirtyBitsMask() const {
    return HdCamera::AllDirty;
}

PXR_NAMESPACE_CLOSE_SCOPE

#endif
