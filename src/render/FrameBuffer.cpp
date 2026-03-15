#include "shiro/render/FrameBuffer.h"

#include <algorithm>
#include <limits>

namespace shiro::render {

void FrameBuffer::Resize(uint32_t width, uint32_t height) {
    width_ = width;
    height_ = height;

    const size_t pixelCount = static_cast<size_t>(width_) * static_cast<size_t>(height_);
    beauty_.assign(pixelCount, Vec4f{});
    albedo_.assign(pixelCount, Vec3f{});
    normal_.assign(pixelCount, Vec3f{});
    depth_.assign(pixelCount, std::numeric_limits<float>::infinity());
}

void FrameBuffer::Clear() {
    std::fill(beauty_.begin(), beauty_.end(), Vec4f{});
    std::fill(albedo_.begin(), albedo_.end(), Vec3f{});
    std::fill(normal_.begin(), normal_.end(), Vec3f{});
    std::fill(depth_.begin(), depth_.end(), std::numeric_limits<float>::infinity());
}

void FrameBuffer::SetBeauty(uint32_t x, uint32_t y, const Vec4f& value) {
    beauty_[PixelOffset(x, y)] = value;
}

void FrameBuffer::SetAlbedo(uint32_t x, uint32_t y, const Vec3f& value) {
    albedo_[PixelOffset(x, y)] = value;
}

void FrameBuffer::SetNormal(uint32_t x, uint32_t y, const Vec3f& value) {
    normal_[PixelOffset(x, y)] = value;
}

void FrameBuffer::SetDepth(uint32_t x, uint32_t y, float value) {
    depth_[PixelOffset(x, y)] = value;
}

size_t FrameBuffer::PixelOffset(uint32_t x, uint32_t y) const {
    return static_cast<size_t>(y) * static_cast<size_t>(width_) + static_cast<size_t>(x);
}

}  // namespace shiro::render
