#pragma once

#include "shiro/core/Config.h"

#if SHIRO_WITH_USD

#include <pxr/imaging/hd/mesh.h>

PXR_NAMESPACE_OPEN_SCOPE

class HdShiroMesh final : public HdMesh {
public:
    explicit HdShiroMesh(const SdfPath& id);
    ~HdShiroMesh() override = default;

    void Sync(HdSceneDelegate* sceneDelegate, HdRenderParam* renderParam, HdDirtyBits* dirtyBits, const TfToken& reprToken) override;
    void Finalize(HdRenderParam* renderParam) override;
    HdDirtyBits GetInitialDirtyBitsMask() const override;

protected:
    void _InitRepr(const TfToken& reprToken, HdDirtyBits* dirtyBits) override;
    HdDirtyBits _PropagateDirtyBits(HdDirtyBits dirtyBits) const override;
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif
