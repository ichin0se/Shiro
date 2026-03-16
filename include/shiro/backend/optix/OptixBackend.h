#pragma once

#include <memory>
#include <mutex>

#include "shiro/backend/RenderBackend.h"

namespace shiro::backend::optix {

class OptixBackend final : public RenderBackend {
public:
    OptixBackend();
    ~OptixBackend() override;

    BackendStatus GetStatus() const override;
    bool SupportsScene(const render::Scene& scene) const;
    render::FrameBuffer RenderSampleBatch(const RenderRequest& request) const override;

    struct RuntimeState;

private:
    bool EnsureRuntime(const RenderRequest& request) const;

    mutable std::mutex mutex_;
    mutable std::unique_ptr<RuntimeState> runtime_;
    mutable bool runtimeAttempted_ = false;
};

}  // namespace shiro::backend::optix
