#include "shiro/backend/RenderBackend.h"
#include "shiro/backend/optix/OptixBackend.h"
#include "shiro/render/Renderer.h"

#include <cstdio>

int main() {
    shiro::backend::optix::OptixBackend backend;
    const shiro::backend::BackendStatus status = backend.GetStatus();
    if (!status.available) {
        std::fprintf(stderr, "optix probe failed: backend unavailable\n");
        return 3;
    }

    shiro::render::RenderSettings settings;
    settings.width = 8;
    settings.height = 8;
    settings.samplesPerPixel = 1;
    settings.samplesPerUpdate = 1;
    settings.backend = shiro::render::BackendKind::Gpu;

    shiro::render::Scene scene;
    scene.environmentTop = {0.2f, 0.4f, 0.8f};
    scene.environmentBottom = {0.05f, 0.05f, 0.05f};
    shiro::render::PbrMaterial material;
    material.baseColor = {0.8f, 0.2f, 0.2f};
    material.roughness = 0.4f;
    scene.materials.push_back(material);
    scene.meshes.push_back({
        {
            {-1.0f, -1.0f, 0.0f},
            {1.0f, -1.0f, 0.0f},
            {0.0f, 1.0f, 0.0f},
        },
        {
            {0.0f, 0.0f, 1.0f},
            {0.0f, 0.0f, 1.0f},
            {0.0f, 0.0f, 1.0f},
        },
        {0u, 1u, 2u},
        0u,
    });
    scene.distantLights.push_back({
        {0.0f, -1.0f, -1.0f},
        {2.0f, 2.0f, 2.0f},
    });

    shiro::render::Camera camera;
    camera.aspectRatio = static_cast<float>(settings.width) / static_cast<float>(settings.height);

    const shiro::backend::RenderRequest request{scene, camera, settings, 0, 1, nullptr};
    const shiro::render::FrameBuffer frame = backend.RenderSampleBatch(request);
    if (frame.Width() == 0 || frame.Height() == 0 || frame.Beauty().empty()) {
        std::fprintf(stderr, "optix probe failed: empty frame\n");
        return 1;
    }

    const shiro::render::Vec4f center =
        frame.Beauty()[static_cast<size_t>(frame.Height() / 2u) * frame.Width() + frame.Width() / 2u];
    if (center.w <= 0.0f || (center.x == 0.0f && center.y == 0.0f && center.z == 0.0f)) {
        std::fprintf(stderr, "optix probe failed: blank frame\n");
        return 2;
    }

    const float centerDepth =
        frame.Depth()[static_cast<size_t>(frame.Height() / 2u) * frame.Width() + frame.Width() / 2u];
    if (!(centerDepth < 1.0e10f)) {
        std::fprintf(stderr, "optix probe failed: no geometry hit\n");
        return 4;
    }

    std::printf(
        "probe center %.4f %.4f %.4f %.4f depth %.4f\n",
        center.x,
        center.y,
        center.z,
        center.w,
        centerDepth);
    return 0;
}
