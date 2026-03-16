#include "shiro/hydra/RenderBuffer.h"

#if SHIRO_WITH_USD

#include <algorithm>
#include <cstring>

#include <pxr/imaging/hd/aov.h>
#include <pxr/imaging/hd/tokens.h>

#include "shiro/hydra/Tokens.h"

PXR_NAMESPACE_OPEN_SCOPE

namespace {

size_t ComponentCount(HdFormat format) {
    switch (format) {
        case HdFormatFloat32:
            return 1;
        case HdFormatFloat32Vec3:
            return 3;
        case HdFormatFloat32Vec4:
        case HdFormatUNorm8Vec4:
            return 4;
        default:
            return 4;
    }
}

size_t BytesPerComponent(HdFormat format) {
    switch (format) {
        case HdFormatUNorm8Vec4:
            return 1;
        case HdFormatFloat32:
        case HdFormatFloat32Vec3:
        case HdFormatFloat32Vec4:
            return sizeof(float);
        default:
            return sizeof(float);
    }
}

}  // namespace

HdShiroRenderBuffer::HdShiroRenderBuffer(const SdfPath& id)
    : HdRenderBuffer(id) {
}

bool HdShiroRenderBuffer::Allocate(const GfVec3i& dimensions, HdFormat format, bool multiSampled) {
    std::scoped_lock lock(mutex_);
    dimensions_ = dimensions;
    format_ = format;
    multiSampled_ = multiSampled;
    ResizeStorage();
    converged_ = false;
    return true;
}

unsigned int HdShiroRenderBuffer::GetWidth() const {
    return static_cast<unsigned int>(dimensions_[0]);
}

unsigned int HdShiroRenderBuffer::GetHeight() const {
    return static_cast<unsigned int>(dimensions_[1]);
}

unsigned int HdShiroRenderBuffer::GetDepth() const {
    return static_cast<unsigned int>(dimensions_[2]);
}

HdFormat HdShiroRenderBuffer::GetFormat() const {
    return format_;
}

bool HdShiroRenderBuffer::IsMultiSampled() const {
    return multiSampled_;
}

void* HdShiroRenderBuffer::Map() {
    mapCount_.fetch_add(1, std::memory_order_relaxed);
    return storage_.data();
}

void HdShiroRenderBuffer::Unmap() {
    size_t expected = mapCount_.load(std::memory_order_relaxed);
    while (expected > 0
        && !mapCount_.compare_exchange_weak(
            expected,
            expected - 1,
            std::memory_order_relaxed,
            std::memory_order_relaxed)) {
    }
}

bool HdShiroRenderBuffer::IsMapped() const {
    return mapCount_.load(std::memory_order_relaxed) > 0;
}

void HdShiroRenderBuffer::Resolve() {
}

bool HdShiroRenderBuffer::IsConverged() const {
    return converged_.load(std::memory_order_relaxed);
}

void HdShiroRenderBuffer::WriteAov(const TfToken& aovName, const shiro::render::FrameBuffer& frame) {
    std::scoped_lock lock(mutex_);

    const size_t framePixelCount = static_cast<size_t>(frame.Width()) * static_cast<size_t>(frame.Height());
    const size_t bufferPixelCount = static_cast<size_t>(GetWidth()) * static_cast<size_t>(GetHeight());
    const size_t pixelCount = std::min(framePixelCount, bufferPixelCount);
    if (pixelCount == 0 || storage_.empty()) {
        return;
    }

    if (format_ == HdFormatFloat32Vec4 && aovName == HdAovTokens->color) {
        std::memcpy(storage_.data(), frame.Beauty().data(), pixelCount * sizeof(shiro::render::Vec4f));
    } else if (format_ == HdFormatUNorm8Vec4 && aovName == HdAovTokens->color) {
        auto* output = storage_.data();
        for (size_t pixelIndex = 0; pixelIndex < pixelCount; ++pixelIndex) {
            const shiro::render::Vec4f source = frame.Beauty()[pixelIndex];
            output[pixelIndex * 4 + 0] = static_cast<uint8_t>(std::clamp(source.x, 0.0f, 1.0f) * 255.0f);
            output[pixelIndex * 4 + 1] = static_cast<uint8_t>(std::clamp(source.y, 0.0f, 1.0f) * 255.0f);
            output[pixelIndex * 4 + 2] = static_cast<uint8_t>(std::clamp(source.z, 0.0f, 1.0f) * 255.0f);
            output[pixelIndex * 4 + 3] = static_cast<uint8_t>(std::clamp(source.w, 0.0f, 1.0f) * 255.0f);
        }
    } else if (format_ == HdFormatFloat32Vec3 && aovName == HdShiroTokens->albedo) {
        std::memcpy(storage_.data(), frame.Albedo().data(), pixelCount * sizeof(shiro::render::Vec3f));
    } else if (format_ == HdFormatFloat32Vec3 && aovName == HdShiroTokens->normal) {
        std::memcpy(storage_.data(), frame.Normal().data(), pixelCount * sizeof(shiro::render::Vec3f));
    } else if (format_ == HdFormatFloat32 && (aovName == HdAovTokens->depth || aovName == HdShiroTokens->depth)) {
        std::memcpy(storage_.data(), frame.Depth().data(), pixelCount * sizeof(float));
    }
}

void HdShiroRenderBuffer::SetConverged(bool converged) {
    converged_.store(converged, std::memory_order_relaxed);
}

size_t HdShiroRenderBuffer::ByteSize() const {
    return static_cast<size_t>(dimensions_[0])
        * static_cast<size_t>(dimensions_[1])
        * static_cast<size_t>(std::max(1, dimensions_[2]))
        * ComponentCount(format_)
        * BytesPerComponent(format_);
}

void HdShiroRenderBuffer::ResizeStorage() {
    storage_.assign(ByteSize(), 0);
}

void HdShiroRenderBuffer::_Deallocate() {
    std::scoped_lock lock(mutex_);
    storage_.clear();
    dimensions_ = GfVec3i(0, 0, 0);
    format_ = HdFormatInvalid;
    multiSampled_ = false;
    mapCount_.store(0, std::memory_order_relaxed);
    converged_.store(false, std::memory_order_relaxed);
}

PXR_NAMESPACE_CLOSE_SCOPE

#endif
