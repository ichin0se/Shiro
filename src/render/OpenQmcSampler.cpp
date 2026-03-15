#include "shiro/render/OpenQmcSampler.h"

#include "shiro/core/Config.h"

#include <memory>
#include <mutex>
#include <random>
#include <cstddef>
#include <vector>

#if SHIRO_WITH_OPENQMC
#include <oqmc/float.h>
#include <oqmc/pmjbn.h>
#endif

namespace shiro::render {

namespace {

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
#else
    std::mt19937 engine;
    std::uniform_real_distribution<float> distribution{0.0f, 1.0f};
#endif
};

OpenQmcSampler::OpenQmcSampler(const SamplerConfig& config)
    : state_(std::make_shared<State>()), config_(config) {
#if !SHIRO_WITH_OPENQMC
    const uint32_t seed = 0x9E3779B9u
        ^ (config.pixelX * 73856093u)
        ^ (config.pixelY * 19349663u)
        ^ (config.sampleIndex * 83492791u);
    state_->engine.seed(seed);
#endif
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
    (void)branch;
    return {
        state_->distribution(state_->engine),
        state_->distribution(state_->engine),
    };
#endif
}

}  // namespace shiro::render
