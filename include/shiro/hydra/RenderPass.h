#pragma once

#include "shiro/core/Config.h"

#if SHIRO_WITH_USD

#include <pxr/imaging/hd/renderPass.h>

PXR_NAMESPACE_OPEN_SCOPE

class HdShiroRenderParam;

class HdShiroRenderPass final : public HdRenderPass {
public:
    HdShiroRenderPass(HdRenderIndex* renderIndex, const HdRprimCollection& collection, HdShiroRenderParam* renderParam);
    ~HdShiroRenderPass() override = default;

    bool IsConverged() const override;

protected:
    void _Execute(const HdRenderPassStateSharedPtr& renderPassState, const TfTokenVector& renderTags) override;

private:
    HdShiroRenderParam* renderParam_ = nullptr;
    bool converged_ = false;
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif
