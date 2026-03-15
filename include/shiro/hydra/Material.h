#pragma once

#include "shiro/core/Config.h"

#if SHIRO_WITH_USD

#include <pxr/imaging/hd/material.h>

PXR_NAMESPACE_OPEN_SCOPE

class HdShiroMaterial final : public HdMaterial {
public:
    explicit HdShiroMaterial(const SdfPath& id);
    ~HdShiroMaterial() override = default;

    void Sync(HdSceneDelegate* sceneDelegate, HdRenderParam* renderParam, HdDirtyBits* dirtyBits) override;
    void Finalize(HdRenderParam* renderParam) override;
    HdDirtyBits GetInitialDirtyBitsMask() const override;
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif
