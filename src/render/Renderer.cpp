#include "shiro/render/Renderer.h"

#include "shiro/backend/RenderBackend.h"
#include "shiro/backend/cpu/CpuPathTracer.h"
#include "shiro/backend/optix/OptixBackend.h"

#include <limits>
#include <utility>

namespace shiro::render {

struct Renderer::Impl {
    backend::cpu::CpuPathTracer cpuBackend;
    backend::optix::OptixBackend optixBackend;

    const backend::RenderBackend& ResolveBackend(
        const Scene& scene,
        const RenderSettings& settings) const {
        const backend::BackendStatus optixStatus = optixBackend.GetStatus();
        const bool canUseOptix = optixStatus.available && optixBackend.SupportsScene(scene);

        if (settings.backend == BackendKind::Gpu && canUseOptix) {
            return optixBackend;
        }

        if (settings.backend == BackendKind::Hybrid && canUseOptix) {
            return optixBackend;
        }

        return cpuBackend;
    }
};

Renderer::Renderer()
    : impl_(std::make_unique<Impl>()) {
}

Renderer::~Renderer() = default;

Renderer::Renderer(Renderer&&) noexcept = default;
Renderer& Renderer::operator=(Renderer&&) noexcept = default;

void Renderer::SetSettings(const RenderSettings& settings) {
    settings_ = settings;
}

void Renderer::FrameAccumulator::Reset(uint32_t width, uint32_t height) {
    width_ = width;
    height_ = height;
    sampleCount_ = 0;

    const size_t pixelCount = static_cast<size_t>(width_) * static_cast<size_t>(height_);
    beautySum_.assign(pixelCount, Vec4f{});
    albedoSum_.assign(pixelCount, Vec3f{});
    normalSum_.assign(pixelCount, Vec3f{});
    depthSum_.assign(pixelCount, 0.0f);
    depthSamples_.assign(pixelCount, 0u);
}

void Renderer::FrameAccumulator::Accumulate(const FrameBuffer& frame, uint32_t sampleCount) {
    if (frame.Width() == 0 || frame.Height() == 0 || sampleCount == 0) {
        return;
    }

    if (frame.Width() != width_ || frame.Height() != height_) {
        Reset(frame.Width(), frame.Height());
    }

    const float weight = static_cast<float>(sampleCount);
    const size_t pixelCount = static_cast<size_t>(width_) * static_cast<size_t>(height_);
    for (size_t pixelIndex = 0; pixelIndex < pixelCount; ++pixelIndex) {
        beautySum_[pixelIndex] = beautySum_[pixelIndex] + frame.Beauty()[pixelIndex] * weight;
        albedoSum_[pixelIndex] = albedoSum_[pixelIndex] + frame.Albedo()[pixelIndex] * weight;
        normalSum_[pixelIndex] = normalSum_[pixelIndex] + frame.Normal()[pixelIndex] * weight;

        if (frame.Depth()[pixelIndex] < std::numeric_limits<float>::infinity()) {
            depthSum_[pixelIndex] += frame.Depth()[pixelIndex] * weight;
            depthSamples_[pixelIndex] += sampleCount;
        }
    }

    sampleCount_ += sampleCount;
}

FrameBuffer Renderer::FrameAccumulator::Resolve() const {
    FrameBuffer frame;
    frame.Resize(width_, height_);

    if (sampleCount_ == 0) {
        return frame;
    }

    const float inverseSampleCount = 1.0f / static_cast<float>(sampleCount_);
    for (uint32_t y = 0; y < height_; ++y) {
        for (uint32_t x = 0; x < width_; ++x) {
            const size_t pixelIndex = static_cast<size_t>(y) * static_cast<size_t>(width_) + static_cast<size_t>(x);
            frame.SetBeauty(x, y, beautySum_[pixelIndex] * inverseSampleCount);
            frame.SetAlbedo(x, y, albedoSum_[pixelIndex] * inverseSampleCount);
            frame.SetNormal(x, y, normalSum_[pixelIndex] * inverseSampleCount);
            frame.SetDepth(
                x,
                y,
                depthSamples_[pixelIndex] > 0
                    ? depthSum_[pixelIndex] / static_cast<float>(depthSamples_[pixelIndex])
                    : std::numeric_limits<float>::infinity());
        }
    }

    return frame;
}

FrameBuffer Renderer::RenderFrame(
    const Scene& scene,
    const Camera& camera,
    const std::atomic<bool>* cancel) const {
    return RenderSampleBatch(scene, camera, 0, settings_.samplesPerPixel, cancel);
}

FrameBuffer Renderer::RenderSampleBatch(
    const Scene& scene,
    const Camera& camera,
    uint32_t sampleStart,
    uint32_t sampleCount,
    const std::atomic<bool>* cancel) const {
    const backend::RenderBackend& backend = impl_->ResolveBackend(scene, settings_);
    backend::RenderRequest request{scene, camera, settings_, sampleStart, sampleCount, cancel};
    return backend.RenderSampleBatch(request);
}

}  // namespace shiro::render
