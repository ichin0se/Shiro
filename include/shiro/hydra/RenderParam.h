#pragma once

#include "shiro/core/Config.h"

#if SHIRO_WITH_USD

#include <mutex>
#include <string>
#include <unordered_map>

#include <pxr/imaging/hd/renderDelegate.h>

#include "shiro/render/Renderer.h"

PXR_NAMESPACE_OPEN_SCOPE

class HdShiroRenderParam final : public HdRenderParam {
public:
    HdShiroRenderParam();

    void SetImageSize(uint32_t width, uint32_t height);
    void SetRenderSetting(const TfToken& key, const VtValue& value);
    VtValue GetRenderSetting(const TfToken& key) const;

    void SetPaused(bool paused);
    bool IsPaused() const;

    void UpsertMesh(const std::string& id, const shiro::render::TriangleMesh& mesh, const shiro::render::PbrMaterial& material);
    void RemoveMesh(const std::string& id);

    shiro::render::FrameBuffer Render(const shiro::render::Camera& camera);
    const shiro::render::RenderSettings& Settings() const { return settings_; }

private:
    shiro::render::Scene BuildSceneSnapshot() const;

    mutable std::mutex mutex_;
    std::unordered_map<std::string, shiro::render::TriangleMesh> meshes_;
    std::unordered_map<std::string, shiro::render::PbrMaterial> materials_;
    shiro::render::RenderSettings settings_;
    shiro::render::Renderer renderer_;
    bool paused_ = false;
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif
