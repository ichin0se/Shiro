#pragma once

#include <cstdint>
#include <memory>

#include "shiro/render/Types.h"

namespace shiro::render {

struct SamplerConfig {
    uint32_t pixelX = 0;
    uint32_t pixelY = 0;
    uint32_t sampleIndex = 0;
};

class OpenQmcSampler {
public:
    explicit OpenQmcSampler(const SamplerConfig& config);

    float Next1D(uint32_t branch);
    Vec2f Next2D(uint32_t branch);

private:
    struct State;
    std::shared_ptr<State> state_;
    SamplerConfig config_;
};

}  // namespace shiro::render
