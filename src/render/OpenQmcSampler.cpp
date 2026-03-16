#include "shiro/render/OpenQmcSampler.h"

#include "shiro/core/Config.h"

#include <cmath>
#include <memory>
#include <mutex>
#include <cstddef>
#include <vector>

#if SHIRO_WITH_OPENQMC
#include <oqmc/float.h>
#include <oqmc/pmjbn.h>
#endif

namespace shiro::render {

namespace {

constexpr uint32_t kPrimeTable[] = {
    2u, 3u, 5u, 7u, 11u, 13u, 17u, 19u,
    23u, 29u, 31u, 37u, 41u, 43u, 47u, 53u,
    59u, 61u, 67u, 71u, 73u, 79u, 83u, 89u,
};
constexpr float kPrimaryR2Step[] = {
    0.7548776662466927f,
    0.5698402909980532f,
};

uint32_t MixBits(uint32_t state) {
    state ^= state >> 16;
    state *= 0x7feb352du;
    state ^= state >> 15;
    state *= 0x846ca68bu;
    state ^= state >> 16;
    return state;
}

float HashToUnitFloat(uint32_t bits) {
    return static_cast<float>(MixBits(bits) & 0x00ffffffu) / 16777216.0f;
}

float RadicalInverse(uint32_t base, uint32_t index) {
    const float inverseBase = 1.0f / static_cast<float>(base);
    float inverseBi = inverseBase;
    float reversed = 0.0f;
    while (index > 0u) {
        const uint32_t digit = index % base;
        reversed += static_cast<float>(digit) * inverseBi;
        index /= base;
        inverseBi *= inverseBase;
    }
    return reversed;
}

float LowDiscrepancySample(uint32_t pixelX, uint32_t pixelY, uint32_t sampleIndex, uint32_t dimension) {
    if (dimension < 2u) {
        const float value = 0.5f + kPrimaryR2Step[dimension] * static_cast<float>(sampleIndex);
        return value - std::floor(value);
    }

    const uint32_t base = kPrimeTable[dimension % (sizeof(kPrimeTable) / sizeof(kPrimeTable[0]))];
    const float sequence = RadicalInverse(base, sampleIndex + 1u);
    const uint32_t rotationKey = pixelX * 0x8da6b343u
        ^ pixelY * 0xd8163841u
        ^ dimension * 0xcb1ab31fu
        ^ 0x9e3779b9u;
    const float rotation = HashToUnitFloat(rotationKey);
    const float value = sequence + rotation;
    return value - std::floor(value);
}

#if SHIRO_WITH_OPENQMC
struct OpenQmcCache {
    OpenQmcCache() {
        data.resize(oqmc::PmjBnSampler::cacheSize);
        oqmc::PmjBnSampler::initialiseCache(data.data());
    }

    std::vector<std::byte> data;
};

std::shared_ptr<OpenQmcCache> AcquireOpenQmcCache() {
    static std::mutex mutex;
    static std::weak_ptr<OpenQmcCache> weakCache;

    std::scoped_lock lock(mutex);
    if (auto shared = weakCache.lock()) {
        return shared;
    }

    auto shared = std::make_shared<OpenQmcCache>();
    weakCache = shared;
    return shared;
}
#endif

}  // namespace

struct OpenQmcSampler::State {
#if SHIRO_WITH_OPENQMC
    std::shared_ptr<OpenQmcCache> cache = AcquireOpenQmcCache();
#endif
};

OpenQmcSampler::OpenQmcSampler(const SamplerConfig& config)
    : state_(std::make_shared<State>()), config_(config) {
}

float OpenQmcSampler::Next1D(uint32_t branch) {
    return Next2D(branch).x;
}

Vec2f OpenQmcSampler::Next2D(uint32_t branch) {
#if SHIRO_WITH_OPENQMC
    oqmc::PmjBnSampler sampler(
        config_.pixelX,
        config_.pixelY,
        branch,
        config_.sampleIndex,
        state_->cache->data.data());

    std::uint32_t sample[2] = {0u, 0u};
    sampler.drawSample<2>(sample);
    return {
        oqmc::uintToFloat(sample[0]),
        oqmc::uintToFloat(sample[1]),
    };
#else
    return {
        LowDiscrepancySample(config_.pixelX, config_.pixelY, config_.sampleIndex, branch * 2u + 0u),
        LowDiscrepancySample(config_.pixelX, config_.pixelY, config_.sampleIndex, branch * 2u + 1u),
    };
#endif
}

}  // namespace shiro::render
