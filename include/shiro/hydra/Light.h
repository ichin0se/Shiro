#pragma once

#include "shiro/core/Config.h"

#if SHIRO_WITH_USD

#include <pxr/imaging/hd/light.h>

PXR_NAMESPACE_OPEN_SCOPE

class HdShiroLight final : public HdLight {
public:
    HdShiroLight(const SdfPath& id, const TfToken& typeId);
    ~HdShiroLight() override = default;

    void Sync(HdSceneDelegate* sceneDelegate, HdRenderParam* renderParam, HdDirtyBits* dirtyBits) override;
    void Finalize(HdRenderParam* renderParam) override;
    HdDirtyBits GetInitialDirtyBitsMask() const override;

private:
    TfToken typeId_;
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif
