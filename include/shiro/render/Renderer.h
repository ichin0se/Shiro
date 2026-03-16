#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "shiro/render/FrameBuffer.h"
#include "shiro/render/Types.h"

namespace shiro::render {

class EnvironmentMap;
struct AccelerationScene;

enum class EnvironmentMapLayout {
    Automatic,
    LatLong,
    Angular,
};

enum class BackendKind {
    Cpu,
    Gpu,
    Hybrid,
};

struct RenderSettings {
    uint32_t width = 1280;
    uint32_t height = 720;
    uint32_t samplesPerPixel = 32;
    uint32_t samplesPerUpdate = 1;
    uint32_t domeLightSamples = 1;
    uint32_t maxDepth = 3;
    uint32_t diffuseDepth = 2;
    uint32_t specularDepth = 2;
    uint32_t threadLimit = 0;
    uint32_t maxSubdivLevel = 2;
    BackendKind backend = BackendKind::Cpu;
    bool backgroundVisible = true;
    bool enableHeadlight = true;
    bool enableCaustics = true;
    bool enableSss = true;
};

struct Camera {
    Vec3f position{0.0f, 0.0f, 5.0f};
    Vec3f forward{0.0f, 0.0f, -1.0f};
    Vec3f right{1.0f, 0.0f, 0.0f};
    Vec3f up{0.0f, 1.0f, 0.0f};
    float verticalFovDegrees = 45.0f;
    float aspectRatio = 16.0f / 9.0f;
};

struct PbrMaterial {
    Vec3f baseColor{0.8f, 0.8f, 0.8f};
    Vec3f emissionColor{0.0f, 0.0f, 0.0f};
    Vec3f specularColor{1.0f, 1.0f, 1.0f};
    Vec3f transmissionColor{1.0f, 1.0f, 1.0f};
    Vec3f transmissionScatter{0.0f, 0.0f, 0.0f};
    Vec3f coatColor{1.0f, 1.0f, 1.0f};
    Vec3f subsurfaceColor{1.0f, 1.0f, 1.0f};
    Vec3f subsurfaceRadius{1.0f, 1.0f, 1.0f};
    Vec3f sheenColor{1.0f, 1.0f, 1.0f};
    Vec3f normalOverride{0.0f, 0.0f, 0.0f};
    Vec3f coatNormalOverride{0.0f, 0.0f, 0.0f};
    Vec3f tangentOverride{0.0f, 0.0f, 0.0f};
    float baseWeight = 1.0f;
    float emissionStrength = 0.0f;
    float specularWeight = 1.0f;
    float metallic = 0.0f;
    float roughness = 0.5f;
    float diffuseRoughness = 0.0f;
    float opacity = 1.0f;
    float specularAnisotropy = 0.0f;
    float specularRotation = 0.0f;
    float coatWeight = 0.0f;
    float coatRoughness = 0.03f;
    float coatIor = 1.5f;
    float coatAnisotropy = 0.0f;
    float coatRotation = 0.0f;
    float sheen = 0.0f;
    float sheenRoughness = 0.3f;
    float subsurface = 0.0f;
    float subsurfaceScale = 1.0f;
    float subsurfaceAnisotropy = 0.0f;
    float transmission = 0.0f;
    float transmissionDepth = 0.0f;
    float transmissionScatterAnisotropy = 0.0f;
    float transmissionDispersion = 0.0f;
    float transmissionExtraRoughness = 0.0f;
    float ior = 1.5f;
    float coatAffectColor = 0.0f;
    float coatAffectRoughness = 0.0f;
    float thinFilmThickness = 0.0f;
    float thinFilmIor = 1.5f;
    bool thinWalled = false;
    bool hasNormalOverride = false;
    bool hasCoatNormalOverride = false;
    bool hasTangentOverride = false;
};

struct TriangleMesh {
    std::vector<Vec3f> positions;
    std::vector<Vec3f> normals;
    std::vector<uint32_t> indices;
    uint32_t materialIndex = 0;
};

struct DirectionalLight {
    Vec3f direction{0.0f, -1.0f, -1.0f};
    Vec3f radiance{2.0f, 2.0f, 2.0f};
};

struct DomeLight {
    Vec3f radiance{1.0f, 1.0f, 1.0f};
    std::string textureFile;
    std::shared_ptr<const EnvironmentMap> environment;
    EnvironmentMapLayout layout = EnvironmentMapLayout::Automatic;
    Vec3f right{1.0f, 0.0f, 0.0f};
    Vec3f up{0.0f, 1.0f, 0.0f};
    Vec3f forward{0.0f, 0.0f, 1.0f};
};

struct Scene {
    std::vector<PbrMaterial> materials;
    std::vector<TriangleMesh> meshes;
    std::vector<DirectionalLight> distantLights;
    std::vector<DomeLight> domeLights;
    Vec3f environmentTop{0.0f, 0.0f, 0.0f};
    Vec3f environmentBottom{0.0f, 0.0f, 0.0f};
};

class Renderer {
public:
    Renderer();
    ~Renderer();

    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;
    Renderer(Renderer&&) noexcept;
    Renderer& operator=(Renderer&&) noexcept;

    void SetSettings(const RenderSettings& settings);
    const RenderSettings& GetSettings() const { return settings_; }

    class FrameAccumulator {
    public:
        void Reset(uint32_t width, uint32_t height);
        void Accumulate(const FrameBuffer& frame, uint32_t sampleCount);
        FrameBuffer Resolve() const;
        uint32_t SampleCount() const { return sampleCount_; }

    private:
        uint32_t width_ = 0;
        uint32_t height_ = 0;
        uint32_t sampleCount_ = 0;
        std::vector<Vec4f> beautySum_;
        std::vector<Vec3f> albedoSum_;
        std::vector<Vec3f> normalSum_;
        std::vector<float> depthSum_;
        std::vector<uint32_t> depthSamples_;
    };

    FrameBuffer RenderFrame(
        const Scene& scene,
        const Camera& camera,
        const std::atomic<bool>* cancel = nullptr) const;
    FrameBuffer RenderSampleBatch(
        const Scene& scene,
        const Camera& camera,
        uint32_t sampleStart,
        uint32_t sampleCount,
        const std::atomic<bool>* cancel = nullptr) const;

private:
    struct Impl;

    RenderSettings settings_;
    std::unique_ptr<Impl> impl_;
};

}  // namespace shiro::render
