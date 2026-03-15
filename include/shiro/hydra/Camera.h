#pragma once

#include "shiro/core/Config.h"

#if SHIRO_WITH_USD

#include <pxr/imaging/hd/camera.h>

PXR_NAMESPACE_OPEN_SCOPE

class HdShiroCamera final : public HdCamera {
public:
    explicit HdShiroCamera(const SdfPath& id);
    ~HdShiroCamera() override = default;

    void Sync(HdSceneDelegate* sceneDelegate, HdRenderParam* renderParam, HdDirtyBits* dirtyBits) override;
    void Finalize(HdRenderParam* renderParam) override;
    HdDirtyBits GetInitialDirtyBitsMask() const override;
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif
