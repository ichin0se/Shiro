#include "shiro/hydra/Light.h"

#if SHIRO_WITH_USD

#include "shiro/hydra/RenderParam.h"
#include "shiro/hydra/SceneBridge.h"

#include <pxr/imaging/hd/changeTracker.h>
#include <pxr/imaging/hd/sceneDelegate.h>
#include <pxr/imaging/hd/tokens.h>

PXR_NAMESPACE_OPEN_SCOPE

HdShiroLight::HdShiroLight(const SdfPath& id, const TfToken& typeId)
    : HdLight(id),
      typeId_(typeId) {
}

void HdShiroLight::Sync(HdSceneDelegate* sceneDelegate, HdRenderParam* renderParam, HdDirtyBits* dirtyBits) {
    auto* shiroRenderParam = static_cast<HdShiroRenderParam*>(renderParam);
    if (!sceneDelegate || !shiroRenderParam) {
        *dirtyBits = HdChangeTracker::Clean;
        return;
    }

    if (typeId_ == HdPrimTypeTokens->domeLight) {
        if (const auto light = HdShiroSceneBridge::ExtractDomeLight(sceneDelegate, GetId())) {
            shiroRenderParam->UpsertDomeLight(GetId().GetString(), *light);
        } else {
            shiroRenderParam->RemoveLight(GetId().GetString());
        }
    } else if (typeId_ == HdPrimTypeTokens->distantLight) {
        if (const auto light = HdShiroSceneBridge::ExtractDistantLight(sceneDelegate, GetId())) {
            shiroRenderParam->UpsertDistantLight(GetId().GetString(), *light);
        } else {
            shiroRenderParam->RemoveLight(GetId().GetString());
        }
    } else {
        shiroRenderParam->RemoveLight(GetId().GetString());
    }

    *dirtyBits = HdChangeTracker::Clean;
}

void HdShiroLight::Finalize(HdRenderParam* renderParam) {
    if (auto* shiroRenderParam = static_cast<HdShiroRenderParam*>(renderParam)) {
        shiroRenderParam->RemoveLight(GetId().GetString());
    }
}

HdDirtyBits HdShiroLight::GetInitialDirtyBitsMask() const {
    return HdLight::AllDirty;
}

PXR_NAMESPACE_CLOSE_SCOPE

#endif
