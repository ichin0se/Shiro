#pragma once

#include <atomic>
#include <cstdint>
#include <string_view>

#include "shiro/render/FrameBuffer.h"
#include "shiro/render/Renderer.h"

namespace shiro::backend {

struct RenderRequest {
    const render::Scene& scene;
    const render::Camera& camera;
    const render::RenderSettings& settings;
    uint32_t sampleStart = 0;
    uint32_t sampleCount = 1;
    const std::atomic<bool>* cancel = nullptr;
};

struct BackendStatus {
    render::BackendKind kind = render::BackendKind::Cpu;
    std::string_view name = "cpu";
    bool available = false;
    bool usesGpu = false;
};

class RenderBackend {
public:
    virtual ~RenderBackend() = default;

    virtual BackendStatus GetStatus() const = 0;
    virtual render::FrameBuffer RenderSampleBatch(const RenderRequest& request) const = 0;
};

std::string_view BackendKindName(render::BackendKind kind);

}  // namespace shiro::backend
