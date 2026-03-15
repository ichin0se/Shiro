#pragma once

#include <cstdint>
#include <vector>

#include "shiro/render/FrameBuffer.h"
#include "shiro/render/Types.h"

namespace shiro::render {

enum class BackendKind {
    Cpu,
    Gpu,
    Hybrid,
};

struct RenderSettings {
    uint32_t width = 1280;
    uint32_t height = 720;
    uint32_t samplesPerPixel = 8;
    uint32_t maxDepth = 6;
    BackendKind backend = BackendKind::Hybrid;
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
    float emissionStrength = 0.0f;
    float metallic = 0.0f;
    float roughness = 0.5f;
    float transmission = 0.0f;
    float ior = 1.5f;
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

struct Scene {
    std::vector<PbrMaterial> materials;
    std::vector<TriangleMesh> meshes;
    std::vector<DirectionalLight> distantLights;
    Vec3f environmentTop{0.05f, 0.08f, 0.12f};
    Vec3f environmentBottom{0.25f, 0.25f, 0.25f};
};

class Renderer {
public:
    void SetSettings(const RenderSettings& settings);
    const RenderSettings& GetSettings() const { return settings_; }

    FrameBuffer RenderFrame(const Scene& scene, const Camera& camera) const;

private:
    RenderSettings settings_;
};

}  // namespace shiro::render
