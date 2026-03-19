#pragma once

#include "shiro/core/Config.h"

#if SHIRO_WITH_USD

#include <chrono>
#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>

#include <pxr/base/vt/dictionary.h>
#include <pxr/imaging/hd/renderDelegate.h>

#include "shiro/render/Renderer.h"

PXR_NAMESPACE_OPEN_SCOPE

class HdShiroRenderParam final : public HdRenderParam {
public:
    HdShiroRenderParam();
    ~HdShiroRenderParam() override;

    void SetImageSize(uint32_t width, uint32_t height);
    void SetRenderSetting(const TfToken& key, const VtValue& value);
    VtValue GetRenderSetting(const TfToken& key) const;

    void SetPaused(bool paused);
    bool IsPaused() const;

    void UpsertMesh(
        const std::string& id,
        const shiro::render::TriangleMesh& mesh,
        const shiro::render::PbrMaterial& fallbackMaterial,
        const std::string& materialId);
    void RemoveMesh(const std::string& id);
    void UpsertMaterial(const std::string& id, const shiro::render::PbrMaterial& material);
    void RemoveMaterial(const std::string& id);
    void UpsertDomeLight(const std::string& id, const shiro::render::DomeLight& light);
    void UpsertDistantLight(const std::string& id, const shiro::render::DirectionalLight& light);
    void RemoveLight(const std::string& id);

    void RequestRender(const shiro::render::Camera& camera);
    bool GetLatestFrame(shiro::render::FrameBuffer* frame, bool* converged) const;
    bool IsConverged() const;
    VtDictionary GetRenderStats() const;
    const shiro::render::RenderSettings& Settings() const { return settings_; }
    uint32_t GetMaxSubdivLevel() const;

private:
    struct MeshRecord {
        shiro::render::TriangleMesh mesh;
        shiro::render::PbrMaterial fallbackMaterial;
        std::string materialId;
    };

    void WorkerLoop();
    std::shared_ptr<shiro::render::Scene> BuildSceneSnapshotLocked();
    void ResolveEnvironmentMaps(shiro::render::Scene* scene) const;
    std::shared_ptr<const shiro::render::EnvironmentMap> LoadEnvironmentMap(const shiro::render::DomeLight& light) const;
    void ResetAccumulationLocked();
    void InvalidateRenderStateLocked(const char* reason);
    void InvalidateSceneStateLocked(const char* reason);

    mutable std::mutex mutex_;
    mutable std::mutex environmentMutex_;
    std::condition_variable workAvailable_;
    std::unordered_map<std::string, MeshRecord> meshes_;
    std::unordered_map<std::string, shiro::render::PbrMaterial> materials_;
    std::unordered_map<std::string, shiro::render::DomeLight> domeLights_;
    std::unordered_map<std::string, shiro::render::DirectionalLight> distantLights_;
    mutable std::unordered_map<std::string, std::weak_ptr<const shiro::render::EnvironmentMap>> environmentCache_;
    std::shared_ptr<shiro::render::Scene> sceneCache_;
    shiro::render::RenderSettings settings_;
    shiro::render::Renderer renderer_;
    shiro::render::Renderer::FrameAccumulator frameAccumulator_;
    shiro::render::FrameBuffer latestFrame_;
    shiro::render::Camera pendingCamera_;
    std::shared_ptr<std::atomic<bool>> activeCancel_;
    std::thread worker_;
    uint64_t sceneVersion_ = 1;
    uint64_t renderVersion_ = 1;
    uint64_t queuedRenderVersion_ = 0;
    uint64_t requestedSerial_ = 0;
    uint64_t completedSerial_ = 0;
    uint32_t accumulatedSamples_ = 0;
    uint32_t activeSampleStart_ = 0;
    uint32_t activeSampleCount_ = 0;
    bool hasPendingCamera_ = false;
    bool hasFrame_ = false;
    bool renderInProgress_ = false;
    bool stopWorker_ = false;
    bool paused_ = false;
    bool sceneCacheDirty_ = true;
    bool hasRenderStartTime_ = false;
    std::chrono::steady_clock::time_point renderStartTime_{};
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif
