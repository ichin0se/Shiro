#include "shiro/hydra/RenderParam.h"

#if SHIRO_WITH_USD

#include "shiro/hydra/Tokens.h"

#include <algorithm>

PXR_NAMESPACE_OPEN_SCOPE

HdShiroRenderParam::HdShiroRenderParam() {
    settings_.backend = shiro::render::BackendKind::Hybrid;
}

void HdShiroRenderParam::SetImageSize(uint32_t width, uint32_t height) {
    std::scoped_lock lock(mutex_);
    settings_.width = width;
    settings_.height = height;
    renderer_.SetSettings(settings_);
}

void HdShiroRenderParam::SetRenderSetting(const TfToken& key, const VtValue& value) {
    std::scoped_lock lock(mutex_);

    if (key == HdShiroTokens->samplesPerPixel && value.IsHolding<int>()) {
        settings_.samplesPerPixel = static_cast<uint32_t>(std::max(1, value.UncheckedGet<int>()));
    } else if (key == HdShiroTokens->maxDepth && value.IsHolding<int>()) {
        settings_.maxDepth = static_cast<uint32_t>(std::max(1, value.UncheckedGet<int>()));
    }

    renderer_.SetSettings(settings_);
}

VtValue HdShiroRenderParam::GetRenderSetting(const TfToken& key) const {
    std::scoped_lock lock(mutex_);

    if (key == HdShiroTokens->samplesPerPixel) {
        return VtValue(static_cast<int>(settings_.samplesPerPixel));
    }

    if (key == HdShiroTokens->maxDepth) {
        return VtValue(static_cast<int>(settings_.maxDepth));
    }

    return {};
}

void HdShiroRenderParam::SetPaused(bool paused) {
    std::scoped_lock lock(mutex_);
    paused_ = paused;
}

bool HdShiroRenderParam::IsPaused() const {
    std::scoped_lock lock(mutex_);
    return paused_;
}

void HdShiroRenderParam::UpsertMesh(
    const std::string& id,
    const shiro::render::TriangleMesh& mesh,
    const shiro::render::PbrMaterial& material) {
    std::scoped_lock lock(mutex_);
    meshes_[id] = mesh;
    materials_[id] = material;
}

void HdShiroRenderParam::RemoveMesh(const std::string& id) {
    std::scoped_lock lock(mutex_);
    meshes_.erase(id);
    materials_.erase(id);
}

shiro::render::FrameBuffer HdShiroRenderParam::Render(const shiro::render::Camera& camera) {
    std::scoped_lock lock(mutex_);
    renderer_.SetSettings(settings_);
    return renderer_.RenderFrame(BuildSceneSnapshot(), camera);
}

shiro::render::Scene HdShiroRenderParam::BuildSceneSnapshot() const {
    shiro::render::Scene scene;
    scene.distantLights.push_back(shiro::render::DirectionalLight{});

    uint32_t materialIndex = 0;
    for (const auto& [id, mesh] : meshes_) {
        shiro::render::TriangleMesh sceneMesh = mesh;
        sceneMesh.materialIndex = materialIndex;
        scene.meshes.push_back(std::move(sceneMesh));

        const auto materialIt = materials_.find(id);
        scene.materials.push_back(materialIt != materials_.end() ? materialIt->second : shiro::render::PbrMaterial{});
        ++materialIndex;
    }

    return scene;
}

PXR_NAMESPACE_CLOSE_SCOPE

#endif
