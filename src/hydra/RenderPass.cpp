#include "shiro/hydra/RenderPass.h"

#if SHIRO_WITH_USD

#include <pxr/base/gf/vec4d.h>
#include <pxr/imaging/hd/renderPassState.h>

#include "shiro/hydra/RenderBuffer.h"
#include "shiro/hydra/RenderParam.h"
#include "shiro/hydra/SceneBridge.h"

PXR_NAMESPACE_OPEN_SCOPE

HdShiroRenderPass::HdShiroRenderPass(
    HdRenderIndex* renderIndex,
    const HdRprimCollection& collection,
    HdShiroRenderParam* renderParam)
    : HdRenderPass(renderIndex, collection), renderParam_(renderParam) {
}

bool HdShiroRenderPass::IsConverged() const {
    return converged_;
}

void HdShiroRenderPass::_Execute(const HdRenderPassStateSharedPtr& renderPassState, const TfTokenVector& renderTags) {
    (void)renderTags;
    if (!renderParam_ || renderParam_->IsPaused()) {
        converged_ = true;
        return;
    }

    const GfVec4d viewport = renderPassState->GetViewport();
    renderParam_->SetImageSize(
        static_cast<uint32_t>(viewport[2]),
        static_cast<uint32_t>(viewport[3]));

    const shiro::render::Camera camera = HdShiroSceneBridge::BuildCamera(renderPassState);
    const shiro::render::FrameBuffer frame = renderParam_->Render(camera);

    for (const auto& aovBinding : renderPassState->GetAovBindings()) {
        auto* renderBuffer = static_cast<HdShiroRenderBuffer*>(aovBinding.renderBuffer);
        if (!renderBuffer) {
            continue;
        }

        renderBuffer->WriteAov(aovBinding.aovName, frame);
        renderBuffer->SetConverged(true);
    }

    converged_ = true;
}

PXR_NAMESPACE_CLOSE_SCOPE

#endif
