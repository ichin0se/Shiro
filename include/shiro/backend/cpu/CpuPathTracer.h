#pragma once

#include <memory>
#include <mutex>

#include "shiro/backend/RenderBackend.h"

namespace shiro::backend::cpu {

struct AccelerationScene;

class CpuPathTracer final : public RenderBackend {
public:
    CpuPathTracer() = default;
    ~CpuPathTracer() override = default;

    BackendStatus GetStatus() const override;
    render::FrameBuffer RenderSampleBatch(const RenderRequest& request) const override;

private:
    std::shared_ptr<const AccelerationScene> GetAccelerationScene(
        const render::Scene& scene,
        const render::RenderSettings& settings) const;

    mutable std::mutex accelerationMutex_;
    mutable const render::Scene* cachedAccelerationSource_ = nullptr;
    mutable std::shared_ptr<const AccelerationScene> cachedAcceleration_;
};

}  // namespace shiro::backend::cpu
