#include "shiro/hydra/RenderParam.h"

#if SHIRO_WITH_USD

#include "shiro/hydra/Tokens.h"
#include "shiro/render/EnvironmentMap.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <optional>
#include <utility>

PXR_NAMESPACE_OPEN_SCOPE

namespace {

constexpr float kScalarEpsilon = 1.0e-4f;
constexpr float kVectorEpsilon = 1.0e-4f;

bool AccumulationDebugEnabled() {
    static const bool enabled = [] {
        const char* value = std::getenv("SHIRO_DEBUG_ACCUMULATION");
        return value && *value != '\0' && *value != '0';
    }();
    return enabled;
}

bool NearlyEqual(float lhs, float rhs, float epsilon = kScalarEpsilon) {
    return std::fabs(lhs - rhs) <= epsilon;
}

bool NearlyEqual(const shiro::render::Vec3f& lhs, const shiro::render::Vec3f& rhs, float epsilon = kVectorEpsilon) {
    return NearlyEqual(lhs.x, rhs.x, epsilon)
        && NearlyEqual(lhs.y, rhs.y, epsilon)
        && NearlyEqual(lhs.z, rhs.z, epsilon);
}

bool NearlyEqualDirections(const shiro::render::Vec3f& lhs, const shiro::render::Vec3f& rhs) {
    return NearlyEqual(lhs, rhs, kVectorEpsilon);
}

bool CamerasEqual(const shiro::render::Camera& lhs, const shiro::render::Camera& rhs) {
    return NearlyEqual(lhs.position, rhs.position)
        && NearlyEqualDirections(lhs.forward, rhs.forward)
        && NearlyEqualDirections(lhs.right, rhs.right)
        && NearlyEqualDirections(lhs.up, rhs.up)
        && NearlyEqual(lhs.verticalFovDegrees, rhs.verticalFovDegrees)
        && NearlyEqual(lhs.aspectRatio, rhs.aspectRatio);
}

bool MaterialsEqual(const shiro::render::PbrMaterial& lhs, const shiro::render::PbrMaterial& rhs) {
    return NearlyEqual(lhs.baseColor, rhs.baseColor)
        && NearlyEqual(lhs.emissionColor, rhs.emissionColor)
        && NearlyEqual(lhs.specularColor, rhs.specularColor)
        && NearlyEqual(lhs.transmissionColor, rhs.transmissionColor)
        && NearlyEqual(lhs.transmissionScatter, rhs.transmissionScatter)
        && NearlyEqual(lhs.coatColor, rhs.coatColor)
        && NearlyEqual(lhs.subsurfaceColor, rhs.subsurfaceColor)
        && NearlyEqual(lhs.subsurfaceRadius, rhs.subsurfaceRadius)
        && NearlyEqual(lhs.sheenColor, rhs.sheenColor)
        && NearlyEqual(lhs.normalOverride, rhs.normalOverride)
        && NearlyEqual(lhs.coatNormalOverride, rhs.coatNormalOverride)
        && NearlyEqual(lhs.tangentOverride, rhs.tangentOverride)
        && NearlyEqual(lhs.baseWeight, rhs.baseWeight)
        && NearlyEqual(lhs.emissionStrength, rhs.emissionStrength)
        && NearlyEqual(lhs.specularWeight, rhs.specularWeight)
        && NearlyEqual(lhs.metallic, rhs.metallic)
        && NearlyEqual(lhs.roughness, rhs.roughness)
        && NearlyEqual(lhs.diffuseRoughness, rhs.diffuseRoughness)
        && NearlyEqual(lhs.opacity, rhs.opacity)
        && NearlyEqual(lhs.specularAnisotropy, rhs.specularAnisotropy)
        && NearlyEqual(lhs.specularRotation, rhs.specularRotation)
        && NearlyEqual(lhs.coatWeight, rhs.coatWeight)
        && NearlyEqual(lhs.coatRoughness, rhs.coatRoughness)
        && NearlyEqual(lhs.coatIor, rhs.coatIor)
        && NearlyEqual(lhs.coatAnisotropy, rhs.coatAnisotropy)
        && NearlyEqual(lhs.coatRotation, rhs.coatRotation)
        && NearlyEqual(lhs.sheen, rhs.sheen)
        && NearlyEqual(lhs.sheenRoughness, rhs.sheenRoughness)
        && NearlyEqual(lhs.subsurface, rhs.subsurface)
        && NearlyEqual(lhs.subsurfaceScale, rhs.subsurfaceScale)
        && NearlyEqual(lhs.subsurfaceAnisotropy, rhs.subsurfaceAnisotropy)
        && NearlyEqual(lhs.transmission, rhs.transmission)
        && NearlyEqual(lhs.transmissionDepth, rhs.transmissionDepth)
        && NearlyEqual(lhs.transmissionScatterAnisotropy, rhs.transmissionScatterAnisotropy)
        && NearlyEqual(lhs.transmissionDispersion, rhs.transmissionDispersion)
        && NearlyEqual(lhs.transmissionExtraRoughness, rhs.transmissionExtraRoughness)
        && NearlyEqual(lhs.ior, rhs.ior)
        && NearlyEqual(lhs.coatAffectColor, rhs.coatAffectColor)
        && NearlyEqual(lhs.coatAffectRoughness, rhs.coatAffectRoughness)
        && NearlyEqual(lhs.thinFilmThickness, rhs.thinFilmThickness)
        && NearlyEqual(lhs.thinFilmIor, rhs.thinFilmIor)
        && lhs.thinWalled == rhs.thinWalled
        && lhs.hasNormalOverride == rhs.hasNormalOverride
        && lhs.hasCoatNormalOverride == rhs.hasCoatNormalOverride
        && lhs.hasTangentOverride == rhs.hasTangentOverride;
}

bool DomeLightsEqual(const shiro::render::DomeLight& lhs, const shiro::render::DomeLight& rhs) {
    return NearlyEqual(lhs.radiance, rhs.radiance)
        && lhs.textureFile == rhs.textureFile
        && lhs.layout == rhs.layout
        && NearlyEqualDirections(lhs.right, rhs.right)
        && NearlyEqualDirections(lhs.up, rhs.up)
        && NearlyEqualDirections(lhs.forward, rhs.forward);
}

bool DirectionalLightsEqual(const shiro::render::DirectionalLight& lhs, const shiro::render::DirectionalLight& rhs) {
    return NearlyEqualDirections(lhs.direction, rhs.direction)
        && NearlyEqual(lhs.radiance, rhs.radiance);
}

bool MeshesEqual(const shiro::render::TriangleMesh& lhs, const shiro::render::TriangleMesh& rhs) {
    if (lhs.materialIndex != rhs.materialIndex
        || lhs.positions.size() != rhs.positions.size()
        || lhs.normals.size() != rhs.normals.size()
        || lhs.indices.size() != rhs.indices.size()) {
        return false;
    }

    for (size_t index = 0; index < lhs.positions.size(); ++index) {
        if (!NearlyEqual(lhs.positions[index], rhs.positions[index])) {
            return false;
        }
    }

    for (size_t index = 0; index < lhs.normals.size(); ++index) {
        if (!NearlyEqual(lhs.normals[index], rhs.normals[index])) {
            return false;
        }
    }

    return lhs.indices == rhs.indices;
}

std::optional<int> ReadIntValue(const VtValue& value) {
    if (value.IsHolding<int>()) {
        return value.UncheckedGet<int>();
    }
    if (value.IsHolding<unsigned int>()) {
        return static_cast<int>(value.UncheckedGet<unsigned int>());
    }
    if (value.IsHolding<unsigned long>()) {
        return static_cast<int>(value.UncheckedGet<unsigned long>());
    }
    if (value.IsHolding<long>()) {
        return static_cast<int>(value.UncheckedGet<long>());
    }
    return std::nullopt;
}

std::optional<bool> ReadBoolValue(const VtValue& value) {
    if (value.IsHolding<bool>()) {
        return value.UncheckedGet<bool>();
    }
    if (const auto intValue = ReadIntValue(value)) {
        return *intValue != 0;
    }
    return std::nullopt;
}

std::optional<std::string> ReadStringValue(const VtValue& value) {
    if (value.IsHolding<std::string>()) {
        return value.UncheckedGet<std::string>();
    }
    if (value.IsHolding<TfToken>()) {
        return value.UncheckedGet<TfToken>().GetString();
    }
    return std::nullopt;
}

std::optional<shiro::render::BackendKind> ReadBackendValue(const VtValue& value) {
    if (const auto intValue = ReadIntValue(value)) {
        switch (*intValue) {
        case 0:
            return shiro::render::BackendKind::Cpu;
        case 1:
            return shiro::render::BackendKind::Gpu;
        case 2:
            return shiro::render::BackendKind::Hybrid;
        default:
            return std::nullopt;
        }
    }

    if (const auto stringValue = ReadStringValue(value)) {
        if (*stringValue == "cpu") {
            return shiro::render::BackendKind::Cpu;
        }
        if (*stringValue == "gpu" || *stringValue == "optix") {
            return shiro::render::BackendKind::Gpu;
        }
        if (*stringValue == "hybrid" || *stringValue == "xpu") {
            return shiro::render::BackendKind::Hybrid;
        }
    }

    return std::nullopt;
}

std::string EnvironmentCacheKey(const shiro::render::DomeLight& light) {
    return light.textureFile + "|" + std::to_string(static_cast<int>(light.layout));
}

}  // namespace

HdShiroRenderParam::HdShiroRenderParam() {
    settings_.backend = shiro::render::BackendKind::Hybrid;
    settings_.samplesPerPixel = 32;
    settings_.samplesPerUpdate = 1;
    settings_.domeLightSamples = 1;
    settings_.maxDepth = 4;
    settings_.diffuseDepth = 2;
    settings_.specularDepth = 2;
    settings_.threadLimit = 0;
    settings_.maxSubdivLevel = 2;
    settings_.backgroundVisible = true;
    settings_.enableHeadlight = true;
    frameAccumulator_.Reset(settings_.width, settings_.height);
    worker_ = std::thread(&HdShiroRenderParam::WorkerLoop, this);
}

HdShiroRenderParam::~HdShiroRenderParam() {
    std::shared_ptr<std::atomic<bool>> cancel;
    {
        std::scoped_lock lock(mutex_);
        stopWorker_ = true;
        cancel = activeCancel_;
    }

    if (cancel) {
        cancel->store(true, std::memory_order_relaxed);
    }
    workAvailable_.notify_one();

    if (worker_.joinable()) {
        worker_.join();
    }
}

void HdShiroRenderParam::SetImageSize(uint32_t width, uint32_t height) {
    std::scoped_lock lock(mutex_);
    if (settings_.width == width && settings_.height == height) {
        return;
    }

    settings_.width = width;
    settings_.height = height;
    InvalidateRenderStateLocked("SetImageSize");
}

void HdShiroRenderParam::SetRenderSetting(const TfToken& key, const VtValue& value) {
    std::scoped_lock lock(mutex_);
    bool changed = false;
    bool sceneStateChanged = false;
    bool renderStateChanged = false;

    if (key == HdShiroTokens->backend || key == HdShiroTokens->namespacedBackend) {
        const auto backendValue = ReadBackendValue(value);
        if (!backendValue) {
            return;
        }
        changed = changed || settings_.backend != *backendValue;
        settings_.backend = *backendValue;
        renderStateChanged = true;
    } else if (key == HdShiroTokens->samplesPerPixel || key == HdShiroTokens->namespacedSamplesPerPixel) {
        const auto intValue = ReadIntValue(value);
        if (!intValue) {
            return;
        }
        const uint32_t samplesPerPixel = static_cast<uint32_t>(std::max(1, *intValue));
        changed = changed || settings_.samplesPerPixel != samplesPerPixel;
        settings_.samplesPerPixel = samplesPerPixel;
        renderStateChanged = true;
    } else if (key == HdShiroTokens->samplesPerUpdate || key == HdShiroTokens->namespacedSamplesPerUpdate) {
        const auto intValue = ReadIntValue(value);
        if (!intValue) {
            return;
        }
        const uint32_t samplesPerUpdate = static_cast<uint32_t>(std::max(1, *intValue));
        changed = changed || settings_.samplesPerUpdate != samplesPerUpdate;
        settings_.samplesPerUpdate = samplesPerUpdate;
        renderStateChanged = true;
    } else if (key == HdShiroTokens->domeLightSamples || key == HdShiroTokens->namespacedDomeLightSamples) {
        const auto intValue = ReadIntValue(value);
        if (!intValue) {
            return;
        }
        const uint32_t domeLightSamples = static_cast<uint32_t>(std::max(1, *intValue));
        changed = changed || settings_.domeLightSamples != domeLightSamples;
        settings_.domeLightSamples = domeLightSamples;
        renderStateChanged = true;
    } else if (key == HdShiroTokens->maxDepth || key == HdShiroTokens->namespacedMaxDepth) {
        const auto intValue = ReadIntValue(value);
        if (!intValue) {
            return;
        }
        const uint32_t maxDepth = static_cast<uint32_t>(std::max(1, *intValue));
        changed = changed || settings_.maxDepth != maxDepth;
        settings_.maxDepth = maxDepth;
        renderStateChanged = true;
    } else if (key == HdShiroTokens->diffuseDepth || key == HdShiroTokens->namespacedDiffuseDepth) {
        const auto intValue = ReadIntValue(value);
        if (!intValue) {
            return;
        }
        const uint32_t diffuseDepth = static_cast<uint32_t>(std::max(0, *intValue));
        changed = changed || settings_.diffuseDepth != diffuseDepth;
        settings_.diffuseDepth = diffuseDepth;
        renderStateChanged = true;
    } else if (key == HdShiroTokens->specularDepth || key == HdShiroTokens->namespacedSpecularDepth) {
        const auto intValue = ReadIntValue(value);
        if (!intValue) {
            return;
        }
        const uint32_t specularDepth = static_cast<uint32_t>(std::max(0, *intValue));
        changed = changed || settings_.specularDepth != specularDepth;
        settings_.specularDepth = specularDepth;
        renderStateChanged = true;
    } else if (key == HdShiroTokens->threadLimit || key == HdShiroTokens->namespacedThreadLimit) {
        const auto intValue = ReadIntValue(value);
        if (!intValue) {
            return;
        }
        const uint32_t threadLimit = static_cast<uint32_t>(std::max(0, *intValue));
        changed = changed || settings_.threadLimit != threadLimit;
        settings_.threadLimit = threadLimit;
        renderStateChanged = true;
    } else if (key == HdShiroTokens->maxSubdivLevel || key == HdShiroTokens->namespacedMaxSubdivLevel) {
        const auto intValue = ReadIntValue(value);
        if (!intValue) {
            return;
        }
        const uint32_t maxSubdivLevel = static_cast<uint32_t>(std::max(0, *intValue));
        changed = changed || settings_.maxSubdivLevel != maxSubdivLevel;
        settings_.maxSubdivLevel = maxSubdivLevel;
        sceneStateChanged = true;
    } else if (key == HdShiroTokens->backgroundVisible || key == HdShiroTokens->namespacedBackgroundVisible) {
        const auto boolValue = ReadBoolValue(value);
        if (!boolValue) {
            return;
        }
        changed = changed || settings_.backgroundVisible != *boolValue;
        settings_.backgroundVisible = *boolValue;
        renderStateChanged = true;
    } else if (key == HdShiroTokens->headlightEnabled || key == HdShiroTokens->namespacedHeadlightEnabled) {
        const auto boolValue = ReadBoolValue(value);
        if (!boolValue) {
            return;
        }
        changed = changed || settings_.enableHeadlight != *boolValue;
        settings_.enableHeadlight = *boolValue;
        sceneStateChanged = true;
    }

    if (changed && sceneStateChanged) {
        InvalidateSceneStateLocked("SetRenderSetting");
    } else if (changed && renderStateChanged) {
        InvalidateRenderStateLocked("SetRenderSetting");
    }
}

VtValue HdShiroRenderParam::GetRenderSetting(const TfToken& key) const {
    std::scoped_lock lock(mutex_);

    if (key == HdShiroTokens->backend || key == HdShiroTokens->namespacedBackend) {
        return VtValue(static_cast<int>(settings_.backend == shiro::render::BackendKind::Cpu
            ? 0
            : settings_.backend == shiro::render::BackendKind::Gpu ? 1 : 2));
    }

    if (key == HdShiroTokens->samplesPerPixel || key == HdShiroTokens->namespacedSamplesPerPixel) {
        return VtValue(static_cast<int>(settings_.samplesPerPixel));
    }

    if (key == HdShiroTokens->samplesPerUpdate || key == HdShiroTokens->namespacedSamplesPerUpdate) {
        return VtValue(static_cast<int>(settings_.samplesPerUpdate));
    }

    if (key == HdShiroTokens->domeLightSamples || key == HdShiroTokens->namespacedDomeLightSamples) {
        return VtValue(static_cast<int>(settings_.domeLightSamples));
    }

    if (key == HdShiroTokens->maxDepth || key == HdShiroTokens->namespacedMaxDepth) {
        return VtValue(static_cast<int>(settings_.maxDepth));
    }

    if (key == HdShiroTokens->diffuseDepth || key == HdShiroTokens->namespacedDiffuseDepth) {
        return VtValue(static_cast<int>(settings_.diffuseDepth));
    }

    if (key == HdShiroTokens->specularDepth || key == HdShiroTokens->namespacedSpecularDepth) {
        return VtValue(static_cast<int>(settings_.specularDepth));
    }

    if (key == HdShiroTokens->threadLimit || key == HdShiroTokens->namespacedThreadLimit) {
        return VtValue(static_cast<int>(settings_.threadLimit));
    }

    if (key == HdShiroTokens->maxSubdivLevel || key == HdShiroTokens->namespacedMaxSubdivLevel) {
        return VtValue(static_cast<int>(settings_.maxSubdivLevel));
    }

    if (key == HdShiroTokens->backgroundVisible || key == HdShiroTokens->namespacedBackgroundVisible) {
        return VtValue(settings_.backgroundVisible);
    }

    if (key == HdShiroTokens->headlightEnabled || key == HdShiroTokens->namespacedHeadlightEnabled) {
        return VtValue(settings_.enableHeadlight);
    }

    return {};
}

void HdShiroRenderParam::SetPaused(bool paused) {
    std::shared_ptr<std::atomic<bool>> cancel;
    {
        std::scoped_lock lock(mutex_);
        if (paused_ == paused) {
            return;
        }

        paused_ = paused;
        cancel = activeCancel_;
        if (!paused_ && hasPendingCamera_) {
            queuedRenderVersion_ = renderVersion_;
            ++requestedSerial_;
            ResetAccumulationLocked();
            if (AccumulationDebugEnabled()) {
                std::fprintf(
                    stderr,
                    "Shiro accumulation reset at SetPaused: requested=%llu completed=%llu renderVersion=%llu queued=%llu sceneVersion=%llu\n",
                    static_cast<unsigned long long>(requestedSerial_),
                    static_cast<unsigned long long>(completedSerial_),
                    static_cast<unsigned long long>(renderVersion_),
                    static_cast<unsigned long long>(queuedRenderVersion_),
                    static_cast<unsigned long long>(sceneVersion_));
            }
        }
    }

    if (cancel) {
        cancel->store(true, std::memory_order_relaxed);
    }
    workAvailable_.notify_one();
}

bool HdShiroRenderParam::IsPaused() const {
    std::scoped_lock lock(mutex_);
    return paused_;
}

void HdShiroRenderParam::UpsertMesh(
    const std::string& id,
    const shiro::render::TriangleMesh& mesh,
    const shiro::render::PbrMaterial& fallbackMaterial,
    const std::string& materialId) {
    std::scoped_lock lock(mutex_);
    const MeshRecord nextRecord{mesh, fallbackMaterial, materialId};
    const auto existingIt = meshes_.find(id);
    if (existingIt != meshes_.end()
        && MeshesEqual(existingIt->second.mesh, nextRecord.mesh)
        && MaterialsEqual(existingIt->second.fallbackMaterial, nextRecord.fallbackMaterial)
        && existingIt->second.materialId == nextRecord.materialId) {
        return;
    }

    meshes_[id] = nextRecord;
    InvalidateSceneStateLocked("UpsertMesh");
}

void HdShiroRenderParam::RemoveMesh(const std::string& id) {
    std::scoped_lock lock(mutex_);
    if (meshes_.erase(id) > 0) {
        InvalidateSceneStateLocked("RemoveMesh");
    }
}

void HdShiroRenderParam::UpsertMaterial(const std::string& id, const shiro::render::PbrMaterial& material) {
    std::scoped_lock lock(mutex_);
    const auto existingIt = materials_.find(id);
    if (existingIt != materials_.end() && MaterialsEqual(existingIt->second, material)) {
        return;
    }

    materials_[id] = material;
    InvalidateSceneStateLocked("UpsertMaterial");
}

void HdShiroRenderParam::RemoveMaterial(const std::string& id) {
    std::scoped_lock lock(mutex_);
    if (materials_.erase(id) > 0) {
        InvalidateSceneStateLocked("RemoveMaterial");
    }
}

void HdShiroRenderParam::UpsertDomeLight(const std::string& id, const shiro::render::DomeLight& light) {
    std::scoped_lock lock(mutex_);
    const auto domeIt = domeLights_.find(id);
    const auto distantIt = distantLights_.find(id);
    if (domeIt != domeLights_.end() && distantIt == distantLights_.end() && DomeLightsEqual(domeIt->second, light)) {
        return;
    }

    domeLights_[id] = light;
    distantLights_.erase(id);
    InvalidateSceneStateLocked("UpsertDomeLight");
}

void HdShiroRenderParam::UpsertDistantLight(const std::string& id, const shiro::render::DirectionalLight& light) {
    std::scoped_lock lock(mutex_);
    const auto domeIt = domeLights_.find(id);
    const auto distantIt = distantLights_.find(id);
    if (domeIt == domeLights_.end()
        && distantIt != distantLights_.end()
        && DirectionalLightsEqual(distantIt->second, light)) {
        return;
    }

    distantLights_[id] = light;
    domeLights_.erase(id);
    InvalidateSceneStateLocked("UpsertDistantLight");
}

void HdShiroRenderParam::RemoveLight(const std::string& id) {
    std::scoped_lock lock(mutex_);
    const size_t erasedDome = domeLights_.erase(id);
    const size_t erasedDistant = distantLights_.erase(id);
    if (erasedDome > 0 || erasedDistant > 0) {
        InvalidateSceneStateLocked("RemoveLight");
    }
}

void HdShiroRenderParam::RequestRender(const shiro::render::Camera& camera) {
    std::shared_ptr<std::atomic<bool>> cancel;
    bool shouldNotify = false;

    {
        std::scoped_lock lock(mutex_);
        if (paused_) {
            return;
        }

        const bool cameraChanged = !hasPendingCamera_ || !CamerasEqual(pendingCamera_, camera);
        const bool renderStateChanged = queuedRenderVersion_ != renderVersion_;

        if (cameraChanged || renderStateChanged || requestedSerial_ == 0) {
            pendingCamera_ = camera;
            hasPendingCamera_ = true;
            queuedRenderVersion_ = renderVersion_;
            ++requestedSerial_;
            ResetAccumulationLocked();
            if (AccumulationDebugEnabled()) {
                std::fprintf(
                    stderr,
                    "Shiro accumulation reset at RequestRender: cameraChanged=%d renderStateChanged=%d requested=%llu completed=%llu renderVersion=%llu queued=%llu sceneVersion=%llu size=%ux%u\n",
                    cameraChanged ? 1 : 0,
                    renderStateChanged ? 1 : 0,
                    static_cast<unsigned long long>(requestedSerial_),
                    static_cast<unsigned long long>(completedSerial_),
                    static_cast<unsigned long long>(renderVersion_),
                    static_cast<unsigned long long>(queuedRenderVersion_),
                    static_cast<unsigned long long>(sceneVersion_),
                    settings_.width,
                    settings_.height);
            }
            cancel = activeCancel_;
            shouldNotify = true;
        } else if (!renderInProgress_ && accumulatedSamples_ < settings_.samplesPerPixel) {
            shouldNotify = true;
        }
    }

    if (cancel) {
        cancel->store(true, std::memory_order_relaxed);
    }
    if (shouldNotify) {
        workAvailable_.notify_one();
    }
}

bool HdShiroRenderParam::GetLatestFrame(shiro::render::FrameBuffer* frame, bool* converged) const {
    std::scoped_lock lock(mutex_);

    if (converged) {
        *converged = paused_
            || (hasFrame_
                && accumulatedSamples_ >= settings_.samplesPerPixel
                && !renderInProgress_
                && completedSerial_ == requestedSerial_);
    }

    if (!hasFrame_) {
        return false;
    }

    if (frame) {
        *frame = latestFrame_;
    }
    return true;
}

bool HdShiroRenderParam::IsConverged() const {
    std::scoped_lock lock(mutex_);
    return paused_
        || (hasFrame_
            && accumulatedSamples_ >= settings_.samplesPerPixel
            && !renderInProgress_
            && completedSerial_ == requestedSerial_);
}

std::shared_ptr<shiro::render::Scene> HdShiroRenderParam::BuildSceneSnapshotLocked() {
    if (!sceneCacheDirty_ && sceneCache_) {
        return sceneCache_;
    }

    auto scene = std::make_shared<shiro::render::Scene>();
    scene->meshes.reserve(meshes_.size());
    scene->materials.reserve(meshes_.size());

    for (const auto& [id, record] : meshes_) {
        shiro::render::TriangleMesh sceneMesh = record.mesh;
        sceneMesh.materialIndex = static_cast<uint32_t>(scene->materials.size());
        scene->meshes.push_back(std::move(sceneMesh));

        shiro::render::PbrMaterial material = record.fallbackMaterial;
        if (!record.materialId.empty()) {
            const auto materialIt = materials_.find(record.materialId);
            if (materialIt != materials_.end()) {
                material = materialIt->second;
            }
        }
        scene->materials.push_back(material);
    }

    scene->domeLights.reserve(domeLights_.size());
    for (const auto& [id, light] : domeLights_) {
        (void)id;
        scene->domeLights.push_back(light);
    }

    scene->distantLights.reserve(distantLights_.size());
    for (const auto& [id, light] : distantLights_) {
        (void)id;
        scene->distantLights.push_back(light);
    }

    if (scene->domeLights.empty() && scene->distantLights.empty() && settings_.enableHeadlight) {
        scene->distantLights.push_back(shiro::render::DirectionalLight{});
        scene->environmentTop = {0.05f, 0.08f, 0.12f};
        scene->environmentBottom = {0.25f, 0.25f, 0.25f};
    }

    sceneCache_ = scene;
    sceneCacheDirty_ = false;
    return sceneCache_;
}

void HdShiroRenderParam::ResolveEnvironmentMaps(shiro::render::Scene* scene) const {
    if (!scene) {
        return;
    }

    for (shiro::render::DomeLight& light : scene->domeLights) {
        if (!light.textureFile.empty() && !light.environment) {
            light.environment = LoadEnvironmentMap(light);
        }
    }
}

std::shared_ptr<const shiro::render::EnvironmentMap> HdShiroRenderParam::LoadEnvironmentMap(
    const shiro::render::DomeLight& light) const {
    if (light.textureFile.empty()) {
        return nullptr;
    }

    const std::string key = EnvironmentCacheKey(light);
    {
        std::scoped_lock lock(environmentMutex_);
        const auto cacheIt = environmentCache_.find(key);
        if (cacheIt != environmentCache_.end()) {
            if (auto shared = cacheIt->second.lock()) {
                return shared;
            }
        }
    }

    std::shared_ptr<shiro::render::EnvironmentMap> loaded =
        shiro::render::EnvironmentMap::Load(light.textureFile, light.layout);
    if (!loaded) {
        return nullptr;
    }

    std::scoped_lock lock(environmentMutex_);
    environmentCache_[key] = loaded;
    return loaded;
}

void HdShiroRenderParam::ResetAccumulationLocked() {
    accumulatedSamples_ = 0;
    frameAccumulator_.Reset(settings_.width, settings_.height);
}

uint32_t HdShiroRenderParam::GetMaxSubdivLevel() const {
    std::scoped_lock lock(mutex_);
    return settings_.maxSubdivLevel;
}

void HdShiroRenderParam::InvalidateRenderStateLocked(const char* reason) {
    ++renderVersion_;
    const bool canRestartRender = !paused_ && hasPendingCamera_;
    if (canRestartRender) {
        queuedRenderVersion_ = renderVersion_;
        ++requestedSerial_;
        ResetAccumulationLocked();
    }
    if (AccumulationDebugEnabled()) {
        std::fprintf(
            stderr,
            "Shiro render invalidated: reason=%s renderVersion=%llu sceneVersion=%llu requested=%llu completed=%llu queuedRestart=%d\n",
            reason ? reason : "unknown",
            static_cast<unsigned long long>(renderVersion_),
            static_cast<unsigned long long>(sceneVersion_),
            static_cast<unsigned long long>(requestedSerial_),
            static_cast<unsigned long long>(completedSerial_),
            canRestartRender ? 1 : 0);
    }
    if (activeCancel_) {
        activeCancel_->store(true, std::memory_order_relaxed);
    }
    if (canRestartRender) {
        workAvailable_.notify_one();
    }
}

void HdShiroRenderParam::InvalidateSceneStateLocked(const char* reason) {
    ++sceneVersion_;
    sceneCacheDirty_ = true;
    InvalidateRenderStateLocked(reason);
}

void HdShiroRenderParam::WorkerLoop() {
    for (;;) {
        shiro::render::Camera camera;
        std::shared_ptr<shiro::render::Scene> scene;
        shiro::render::RenderSettings settings;
        uint64_t serial = 0;
        uint32_t sampleStart = 0;
        uint32_t sampleCount = 0;
        std::shared_ptr<std::atomic<bool>> cancel = std::make_shared<std::atomic<bool>>(false);

        {
            std::unique_lock lock(mutex_);
            workAvailable_.wait(lock, [&] {
                return stopWorker_
                    || (!paused_ && hasPendingCamera_ && !renderInProgress_ && requestedSerial_ > completedSerial_);
            });

            if (stopWorker_) {
                return;
            }

            renderInProgress_ = true;
            activeCancel_ = cancel;
            serial = requestedSerial_;
            settings = settings_;
            camera = pendingCamera_;
            scene = BuildSceneSnapshotLocked();
            sampleStart = accumulatedSamples_;
            if (settings.samplesPerPixel > accumulatedSamples_) {
                sampleCount = std::min(settings.samplesPerUpdate, settings.samplesPerPixel - accumulatedSamples_);
            }
        }

        ResolveEnvironmentMaps(scene.get());

        if (sampleCount == 0) {
            std::scoped_lock lock(mutex_);
            renderInProgress_ = false;
            completedSerial_ = serial;
            if (activeCancel_ == cancel) {
                activeCancel_.reset();
            }
            continue;
        }

        renderer_.SetSettings(settings);
        shiro::render::FrameBuffer frame = renderer_.RenderSampleBatch(*scene, camera, sampleStart, sampleCount, cancel.get());
        const bool wasCancelled = cancel->load(std::memory_order_relaxed);

        bool shouldContinue = false;
        {
            std::scoped_lock lock(mutex_);
            renderInProgress_ = false;

            if (!wasCancelled && serial == requestedSerial_) {
                if (sampleStart == 0 || frameAccumulator_.SampleCount() == 0) {
                    frameAccumulator_.Reset(settings.width, settings.height);
                }

                frameAccumulator_.Accumulate(frame, sampleCount);
                latestFrame_ = frameAccumulator_.Resolve();
                hasFrame_ = latestFrame_.Width() > 0 && latestFrame_.Height() > 0;
                accumulatedSamples_ += sampleCount;
                if (AccumulationDebugEnabled()) {
                    std::fprintf(
                        stderr,
                        "Shiro accumulation progress: serial=%llu sampleStart=%u sampleCount=%u accumulated=%u target=%u\n",
                        static_cast<unsigned long long>(serial),
                        sampleStart,
                        sampleCount,
                        accumulatedSamples_,
                        settings_.samplesPerPixel);
                }

                if (accumulatedSamples_ >= settings_.samplesPerPixel) {
                    completedSerial_ = serial;
                } else {
                    shouldContinue = true;
                }
            } else {
                shouldContinue = !paused_ && hasPendingCamera_ && requestedSerial_ > completedSerial_;
            }

            if (activeCancel_ == cancel) {
                activeCancel_.reset();
            }
        }

        if (!stopWorker_ && shouldContinue) {
            workAvailable_.notify_one();
        }
    }
}

PXR_NAMESPACE_CLOSE_SCOPE

#endif
