#include "shiro/hydra/Material.h"

#if SHIRO_WITH_USD

#include "shiro/hydra/RenderParam.h"
#include "shiro/hydra/SceneBridge.h"

#include <pxr/imaging/hd/changeTracker.h>
#include <pxr/imaging/hd/sceneDelegate.h>

PXR_NAMESPACE_OPEN_SCOPE

HdShiroMaterial::HdShiroMaterial(const SdfPath& id)
    : HdMaterial(id) {
}

void HdShiroMaterial::Sync(HdSceneDelegate* sceneDelegate, HdRenderParam* renderParam, HdDirtyBits* dirtyBits) {
    auto* shiroRenderParam = static_cast<HdShiroRenderParam*>(renderParam);
    if (!sceneDelegate || !shiroRenderParam) {
        *dirtyBits = HdChangeTracker::Clean;
        return;
    }

    const VtValue materialResource = sceneDelegate->GetMaterialResource(GetId());
    if (const auto material = HdShiroSceneBridge::ExtractMaterial(materialResource)) {
        shiroRenderParam->UpsertMaterial(GetId().GetString(), *material);
    } else {
        shiroRenderParam->RemoveMaterial(GetId().GetString());
    }

    *dirtyBits = HdChangeTracker::Clean;
}

void HdShiroMaterial::Finalize(HdRenderParam* renderParam) {
    if (auto* shiroRenderParam = static_cast<HdShiroRenderParam*>(renderParam)) {
        shiroRenderParam->RemoveMaterial(GetId().GetString());
    }
}

HdDirtyBits HdShiroMaterial::GetInitialDirtyBitsMask() const {
    return HdChangeTracker::AllDirty;
}

PXR_NAMESPACE_CLOSE_SCOPE

#endif
