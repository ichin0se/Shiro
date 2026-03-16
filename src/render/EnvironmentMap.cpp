#include "shiro/render/EnvironmentMap.h"

#include "shiro/core/Config.h"

#include <algorithm>
#include <cstddef>
#include <cmath>
#include <limits>
#include <utility>

#if SHIRO_HAVE_OIIO
#include <OpenImageIO/imageio.h>
#endif

namespace shiro::render {

namespace {

constexpr float kPi = 3.14159265358979323846f;
constexpr float kTwoPi = 2.0f * kPi;
constexpr float kInvFourPi = 1.0f / (4.0f * kPi);

float WrapUnit(float value) {
    value = std::fmod(value, 1.0f);
    return value < 0.0f ? value + 1.0f : value;
}

float Luminance(const Vec3f& value) {
    return value.x * 0.2126f + value.y * 0.7152f + value.z * 0.0722f;
}

Vec3f SampleUniformSphere(const Vec2f& sample) {
    const float y = 1.0f - 2.0f * sample.y;
    const float radius = std::sqrt(std::max(0.0f, 1.0f - y * y));
    const float phi = kTwoPi * sample.x;
    return {
        std::sin(phi) * radius,
        y,
        std::cos(phi) * radius,
    };
}

EnvironmentMapLayout ResolveLayout(EnvironmentMapLayout layout) {
    return layout == EnvironmentMapLayout::Automatic ? EnvironmentMapLayout::LatLong : layout;
}

}  // namespace

std::shared_ptr<EnvironmentMap> EnvironmentMap::Load(const std::string& filePath, EnvironmentMapLayout layout) {
#if SHIRO_HAVE_OIIO
    auto input = OIIO::ImageInput::open(filePath);
    if (!input) {
        return nullptr;
    }

    const OIIO::ImageSpec& spec = input->spec();
    if (spec.width <= 0 || spec.height <= 0 || spec.nchannels <= 0) {
        return nullptr;
    }

    std::vector<float> pixels(static_cast<size_t>(spec.width) * static_cast<size_t>(spec.height) * static_cast<size_t>(spec.nchannels));
    if (!input->read_image(OIIO::TypeDesc::FLOAT, pixels.data())) {
        return nullptr;
    }
    input->close();

    auto environment = std::shared_ptr<EnvironmentMap>(new EnvironmentMap());
    environment->width_ = static_cast<uint32_t>(spec.width);
    environment->height_ = static_cast<uint32_t>(spec.height);
    environment->layout_ = ResolveLayout(layout);
    environment->pixels_.resize(static_cast<size_t>(environment->width_) * static_cast<size_t>(environment->height_));

    for (uint32_t y = 0; y < environment->height_; ++y) {
        for (uint32_t x = 0; x < environment->width_; ++x) {
            const size_t pixelIndex = static_cast<size_t>(y) * static_cast<size_t>(environment->width_) + static_cast<size_t>(x);
            const size_t sourceIndex = pixelIndex * static_cast<size_t>(spec.nchannels);

            const float r = pixels[sourceIndex + 0];
            const float g = spec.nchannels > 1 ? pixels[sourceIndex + 1] : r;
            const float b = spec.nchannels > 2 ? pixels[sourceIndex + 2] : g;
            environment->pixels_[pixelIndex] = {r, g, b};
        }
    }

    environment->BuildImportanceTable();
    return environment;
#else
    (void)filePath;
    (void)layout;
    return nullptr;
#endif
}

Vec3f EnvironmentMap::Evaluate(const Vec3f& localDirection) const {
    if (pixels_.empty()) {
        return {};
    }

    switch (layout_) {
        case EnvironmentMapLayout::Angular:
            return EvaluateAngular(localDirection);
        case EnvironmentMapLayout::Automatic:
        case EnvironmentMapLayout::LatLong:
        default:
            return EvaluateLatLong(localDirection);
    }
}

Vec3f EnvironmentMap::Sample(const Vec2f& sample, Vec3f* localDirection, float* pdf) const {
    if (pixels_.empty()) {
        if (localDirection) {
            *localDirection = {0.0f, 1.0f, 0.0f};
        }
        if (pdf) {
            *pdf = 0.0f;
        }
        return {};
    }

    if (layout_ == EnvironmentMapLayout::LatLong && !rowCdf_.empty() && width_ > 0 && height_ > 0) {
        float rowMass = 0.0f;
        const uint32_t row = SampleCdf(rowCdf_, 0, height_, sample.y, &rowMass);
        const uint32_t conditionalOffset = row * (width_ + 1u);
        float columnMass = 0.0f;
        const uint32_t column = SampleCdf(
            conditionalCdf_,
            conditionalOffset,
            conditionalOffset + width_,
            sample.x,
            &columnMass);

        const float u = (static_cast<float>(column) + 0.5f) / static_cast<float>(width_);
        const float v = (static_cast<float>(row) + 0.5f) / static_cast<float>(height_);
        const float theta = v * kPi;
        const float phi = u * kTwoPi;
        const float sinTheta = std::sin(theta);
        const Vec3f direction{
            std::sin(phi) * sinTheta,
            std::cos(theta),
            std::cos(phi) * sinTheta,
        };

        if (localDirection) {
            *localDirection = direction;
        }
        if (pdf) {
            const float pixelMass = rowMass * columnMass;
            *pdf = sinTheta > 1.0e-6f
                ? pixelMass * static_cast<float>(width_ * height_) / (2.0f * kPi * kPi * sinTheta)
                : 0.0f;
        }
        return Evaluate(direction);
    }

    const Vec3f direction = SampleUniformSphere(sample);
    if (localDirection) {
        *localDirection = direction;
    }
    if (pdf) {
        *pdf = kInvFourPi;
    }
    return Evaluate(direction);
}

float EnvironmentMap::Pdf(const Vec3f& localDirection) const {
    if (pixels_.empty()) {
        return 0.0f;
    }

    if (layout_ != EnvironmentMapLayout::LatLong || rowCdf_.empty() || width_ == 0 || height_ == 0) {
        return kInvFourPi;
    }

    const Vec3f direction = Normalize(localDirection);
    const float phi = std::atan2(direction.x, direction.z);
    const float theta = std::acos(Clamp(direction.y, -1.0f, 1.0f));
    const float u = WrapUnit(phi / kTwoPi);
    const float v = Clamp(theta / kPi, 0.0f, 1.0f);

    const uint32_t x = std::min(static_cast<uint32_t>(u * static_cast<float>(width_)), width_ - 1u);
    const uint32_t y = std::min(static_cast<uint32_t>(v * static_cast<float>(height_)), height_ - 1u);
    return LatLongPdf(x, y, std::sin(theta));
}

bool EnvironmentMap::BuildImportanceTable() {
    rowCdf_.clear();
    conditionalCdf_.clear();

    if (pixels_.empty() || width_ == 0 || height_ == 0 || layout_ != EnvironmentMapLayout::LatLong) {
        return false;
    }

    rowCdf_.assign(height_ + 1u, 0.0f);
    conditionalCdf_.assign(static_cast<size_t>(height_) * static_cast<size_t>(width_ + 1u), 0.0f);

    float totalWeight = 0.0f;
    for (uint32_t y = 0; y < height_; ++y) {
        const float theta = (static_cast<float>(y) + 0.5f) * kPi / static_cast<float>(height_);
        const float sinTheta = std::max(std::sin(theta), 1.0e-6f);

        const size_t conditionalOffset = static_cast<size_t>(y) * static_cast<size_t>(width_ + 1u);
        float rowWeight = 0.0f;
        for (uint32_t x = 0; x < width_; ++x) {
            const float weight = std::max(0.0f, Luminance(pixels_[static_cast<size_t>(y) * static_cast<size_t>(width_) + static_cast<size_t>(x)])) * sinTheta;
            rowWeight += weight;
            conditionalCdf_[conditionalOffset + static_cast<size_t>(x) + 1u] = rowWeight;
        }

        if (rowWeight > 0.0f) {
            for (uint32_t x = 1; x <= width_; ++x) {
                conditionalCdf_[conditionalOffset + static_cast<size_t>(x)] /= rowWeight;
            }
        } else {
            for (uint32_t x = 1; x <= width_; ++x) {
                conditionalCdf_[conditionalOffset + static_cast<size_t>(x)] = static_cast<float>(x) / static_cast<float>(width_);
            }
            rowWeight = 1.0f;
        }

        totalWeight += rowWeight;
        rowCdf_[y + 1u] = totalWeight;
    }

    if (totalWeight <= 0.0f) {
        for (uint32_t y = 1; y <= height_; ++y) {
            rowCdf_[y] = static_cast<float>(y) / static_cast<float>(height_);
        }
        return true;
    }

    for (uint32_t y = 1; y <= height_; ++y) {
        rowCdf_[y] /= totalWeight;
    }

    return true;
}

Vec3f EnvironmentMap::EvaluateLatLong(const Vec3f& localDirection) const {
    const Vec3f direction = Normalize(localDirection);
    const float phi = std::atan2(direction.x, direction.z);
    const float theta = std::acos(Clamp(direction.y, -1.0f, 1.0f));
    const float u = WrapUnit(phi / kTwoPi);
    const float v = Clamp(theta / kPi, 0.0f, 1.0f);
    return BilinearSample(u, v);
}

Vec3f EnvironmentMap::EvaluateAngular(const Vec3f& localDirection) const {
    const Vec3f direction = Normalize(localDirection);
    const float radialLength = std::sqrt(direction.x * direction.x + direction.y * direction.y);
    if (radialLength <= 1.0e-6f) {
        return BilinearSample(0.5f, 0.5f);
    }

    const float angle = std::acos(Clamp(direction.z, -1.0f, 1.0f));
    const float radius = angle / kPi;
    const float normalizedX = direction.x / radialLength;
    const float normalizedY = direction.y / radialLength;
    const float u = 0.5f + 0.5f * radius * normalizedX;
    const float v = 0.5f + 0.5f * radius * normalizedY;
    if (u < 0.0f || u > 1.0f || v < 0.0f || v > 1.0f) {
        return {};
    }
    return BilinearSample(u, v);
}

Vec3f EnvironmentMap::BilinearSample(float u, float v) const {
    if (pixels_.empty() || width_ == 0 || height_ == 0) {
        return {};
    }

    u = WrapUnit(u) * static_cast<float>(width_) - 0.5f;
    v = Clamp(v, 0.0f, 1.0f) * static_cast<float>(height_) - 0.5f;

    const int x0 = static_cast<int>(std::floor(u));
    const int y0 = static_cast<int>(std::floor(v));
    const int x1 = x0 + 1;
    const int y1 = std::min(y0 + 1, static_cast<int>(height_) - 1);
    const float tx = u - static_cast<float>(x0);
    const float ty = v - static_cast<float>(y0);

    const auto wrappedX = [&](int x) {
        const int width = static_cast<int>(width_);
        int wrapped = x % width;
        return wrapped < 0 ? wrapped + width : wrapped;
    };
    const auto clampedY = [&](int y) {
        return std::clamp(y, 0, static_cast<int>(height_) - 1);
    };
    const auto load = [&](int x, int y) {
        return pixels_[static_cast<size_t>(clampedY(y)) * static_cast<size_t>(width_) + static_cast<size_t>(wrappedX(x))];
    };

    const Vec3f c00 = load(x0, y0);
    const Vec3f c10 = load(x1, y0);
    const Vec3f c01 = load(x0, y1);
    const Vec3f c11 = load(x1, y1);
    const Vec3f c0 = Lerp(c00, c10, tx);
    const Vec3f c1 = Lerp(c01, c11, tx);
    return Lerp(c0, c1, ty);
}

float EnvironmentMap::LatLongPdf(uint32_t x, uint32_t y, float sinTheta) const {
    if (rowCdf_.empty() || conditionalCdf_.empty() || width_ == 0 || height_ == 0 || sinTheta <= 1.0e-6f) {
        return 0.0f;
    }

    const float rowMass = rowCdf_[y + 1u] - rowCdf_[y];
    const size_t conditionalOffset = static_cast<size_t>(y) * static_cast<size_t>(width_ + 1u);
    const float columnMass = conditionalCdf_[conditionalOffset + static_cast<size_t>(x) + 1u]
        - conditionalCdf_[conditionalOffset + static_cast<size_t>(x)];
    const float pixelMass = rowMass * columnMass;
    return pixelMass * static_cast<float>(width_ * height_) / (2.0f * kPi * kPi * sinTheta);
}

uint32_t EnvironmentMap::SampleCdf(
    const std::vector<float>& cdf,
    uint32_t begin,
    uint32_t end,
    float sample,
    float* pdfMass) const {
    const auto first = cdf.begin() + static_cast<std::ptrdiff_t>(begin + 1u);
    const auto last = cdf.begin() + static_cast<std::ptrdiff_t>(end + 1u);
    const auto it = std::lower_bound(first, last, Clamp(sample, 0.0f, std::nextafter(1.0f, 0.0f)));
    const uint32_t index = static_cast<uint32_t>(std::distance(cdf.begin(), it)) - 1u;

    if (pdfMass) {
        *pdfMass = cdf[index + 1u] - cdf[index];
    }
    return index - begin;
}

}  // namespace shiro::render
