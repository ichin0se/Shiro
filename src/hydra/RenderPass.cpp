#include "shiro/hydra/RenderPass.h"

#if SHIRO_WITH_USD

#include <algorithm>

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
    return renderParam_ ? renderParam_->IsConverged() : converged_;
}

void HdShiroRenderPass::_Execute(const HdRenderPassStateSharedPtr& renderPassState, const TfTokenVector& renderTags) {
    (void)renderTags;
    if (!renderParam_ || renderParam_->IsPaused()) {
        converged_ = true;
        return;
    }

    uint32_t imageWidth = 0;
    uint32_t imageHeight = 0;
    for (const auto& aovBinding : renderPassState->GetAovBindings()) {
        auto* renderBuffer = static_cast<HdShiroRenderBuffer*>(aovBinding.renderBuffer);
        if (!renderBuffer) {
            continue;
        }

        imageWidth = std::max(imageWidth, renderBuffer->GetWidth());
        imageHeight = std::max(imageHeight, renderBuffer->GetHeight());
    }

    if (imageWidth == 0 || imageHeight == 0) {
        const GfVec4d viewport = renderPassState->GetViewport();
        imageWidth = static_cast<uint32_t>(viewport[2]);
        imageHeight = static_cast<uint32_t>(viewport[3]);
    }

    if (imageWidth == 0 || imageHeight == 0) {
        converged_ = true;
        return;
    }

    renderParam_->SetImageSize(imageWidth, imageHeight);

    const shiro::render::Camera camera = HdShiroSceneBridge::BuildCamera(renderPassState, imageWidth, imageHeight);
    renderParam_->RequestRender(camera);

    shiro::render::FrameBuffer frame;
    bool renderConverged = false;
    const bool hasFrame = renderParam_->GetLatestFrame(&frame, &renderConverged);

    for (const auto& aovBinding : renderPassState->GetAovBindings()) {
        auto* renderBuffer = static_cast<HdShiroRenderBuffer*>(aovBinding.renderBuffer);
        if (!renderBuffer) {
            continue;
        }

        if (hasFrame) {
            renderBuffer->WriteAov(aovBinding.aovName, frame);
        }
        renderBuffer->SetConverged(renderConverged);
    }

    converged_ = renderConverged;
}

PXR_NAMESPACE_CLOSE_SCOPE

#endif
