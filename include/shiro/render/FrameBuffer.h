#pragma once

#include <cstdint>
#include <vector>

#include "shiro/render/Types.h"

namespace shiro::render {

class FrameBuffer {
public:
    void Resize(uint32_t width, uint32_t height);
    void Clear();

    void SetBeauty(uint32_t x, uint32_t y, const Vec4f& value);
    void SetAlbedo(uint32_t x, uint32_t y, const Vec3f& value);
    void SetNormal(uint32_t x, uint32_t y, const Vec3f& value);
    void SetDepth(uint32_t x, uint32_t y, float value);

    uint32_t Width() const { return width_; }
    uint32_t Height() const { return height_; }

    const std::vector<Vec4f>& Beauty() const { return beauty_; }
    const std::vector<Vec3f>& Albedo() const { return albedo_; }
    const std::vector<Vec3f>& Normal() const { return normal_; }
    const std::vector<float>& Depth() const { return depth_; }

private:
    size_t PixelOffset(uint32_t x, uint32_t y) const;

    uint32_t width_ = 0;
    uint32_t height_ = 0;
    std::vector<Vec4f> beauty_;
    std::vector<Vec3f> albedo_;
    std::vector<Vec3f> normal_;
    std::vector<float> depth_;
};

}  // namespace shiro::render
