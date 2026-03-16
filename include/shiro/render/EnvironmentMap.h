#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "shiro/render/Renderer.h"
#include "shiro/render/Types.h"

namespace shiro::render {

class EnvironmentMap {
public:
    static std::shared_ptr<EnvironmentMap> Load(const std::string& filePath, EnvironmentMapLayout layout);

    Vec3f Evaluate(const Vec3f& localDirection) const;
    Vec3f Sample(const Vec2f& sample, Vec3f* localDirection, float* pdf) const;
    float Pdf(const Vec3f& localDirection) const;

    uint32_t Width() const { return width_; }
    uint32_t Height() const { return height_; }
    EnvironmentMapLayout Layout() const { return layout_; }
    const std::vector<Vec3f>& Pixels() const { return pixels_; }
    const std::vector<float>& RowCdf() const { return rowCdf_; }
    const std::vector<float>& ConditionalCdf() const { return conditionalCdf_; }

private:
    EnvironmentMap() = default;

    bool BuildImportanceTable();
    Vec3f EvaluateLatLong(const Vec3f& localDirection) const;
    Vec3f EvaluateAngular(const Vec3f& localDirection) const;
    Vec3f BilinearSample(float u, float v) const;
    float LatLongPdf(uint32_t x, uint32_t y, float sinTheta) const;
    uint32_t SampleCdf(
        const std::vector<float>& cdf,
        uint32_t begin,
        uint32_t end,
        float sample,
        float* pdfMass) const;

    uint32_t width_ = 0;
    uint32_t height_ = 0;
    EnvironmentMapLayout layout_ = EnvironmentMapLayout::Automatic;
    std::vector<Vec3f> pixels_;
    std::vector<float> rowCdf_;
    std::vector<float> conditionalCdf_;
};

}  // namespace shiro::render
