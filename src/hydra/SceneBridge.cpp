#include "shiro/hydra/SceneBridge.h"

#if SHIRO_WITH_USD

#include <cmath>

#include <pxr/base/gf/matrix4d.h>
#include <pxr/base/gf/vec3d.h>
#include <pxr/base/gf/vec3f.h>
#include <pxr/base/gf/vec4d.h>
#include <pxr/base/vt/array.h>
#include <pxr/base/vt/value.h>
#include <pxr/imaging/hd/meshTopology.h>
#include <pxr/imaging/hd/renderPassState.h>
#include <pxr/imaging/hd/sceneDelegate.h>
#include <pxr/imaging/hd/tokens.h>

PXR_NAMESPACE_OPEN_SCOPE

namespace {

shiro::render::Vec3f ToVec3f(const GfVec3f& value) {
    return {value[0], value[1], value[2]};
}

shiro::render::Vec3f ToVec3f(const GfVec3d& value) {
    return {
        static_cast<float>(value[0]),
        static_cast<float>(value[1]),
        static_cast<float>(value[2]),
    };
}

shiro::render::Vec3f TransformPoint(const GfMatrix4d& matrix, const shiro::render::Vec3f& point) {
    return ToVec3f(matrix.Transform(GfVec3d(point.x, point.y, point.z)));
}

}  // namespace

std::optional<HdShiroMeshPayload> HdShiroSceneBridge::ExtractMesh(HdSceneDelegate* sceneDelegate, const SdfPath& id) {
    const VtValue pointsValue = sceneDelegate->Get(id, HdTokens->points);
    const VtValue topologyValue = sceneDelegate->Get(id, HdTokens->topology);

    if (!pointsValue.IsHolding<VtVec3fArray>() || !topologyValue.IsHolding<HdMeshTopology>()) {
        return std::nullopt;
    }

    const GfMatrix4d transform = sceneDelegate->GetTransform(id);
    const VtVec3fArray& points = pointsValue.UncheckedGet<VtVec3fArray>();
    const HdMeshTopology& topology = topologyValue.UncheckedGet<HdMeshTopology>();
    const VtIntArray& faceVertexCounts = topology.GetFaceVertexCounts();
    const VtIntArray& faceVertexIndices = topology.GetFaceVertexIndices();

    HdShiroMeshPayload payload;
    payload.mesh.positions.reserve(points.size());

    for (const GfVec3f& point : points) {
        payload.mesh.positions.push_back(TransformPoint(transform, ToVec3f(point)));
    }

    size_t faceIndexOffset = 0;
    for (const int faceVertexCount : faceVertexCounts) {
        if (faceVertexCount >= 3) {
            const uint32_t rootIndex = static_cast<uint32_t>(faceVertexIndices[faceIndexOffset]);
            for (int vertex = 1; vertex < faceVertexCount - 1; ++vertex) {
                payload.mesh.indices.push_back(rootIndex);
                payload.mesh.indices.push_back(static_cast<uint32_t>(faceVertexIndices[faceIndexOffset + vertex]));
                payload.mesh.indices.push_back(static_cast<uint32_t>(faceVertexIndices[faceIndexOffset + vertex + 1]));
            }
        }
        faceIndexOffset += static_cast<size_t>(faceVertexCount);
    }

    payload.material = shiro::render::PbrMaterial{};

    const VtValue colorValue = sceneDelegate->Get(id, HdTokens->displayColor);
    if (colorValue.IsHolding<VtVec3fArray>() && !colorValue.UncheckedGet<VtVec3fArray>().empty()) {
        payload.material.baseColor = ToVec3f(colorValue.UncheckedGet<VtVec3fArray>()[0]);
    }

    const TfToken metallicToken("metallic");
    const TfToken roughnessToken("roughness");
    const TfToken emissionToken("emissiveColor");

    const VtValue metallicValue = sceneDelegate->Get(id, metallicToken);
    if (metallicValue.IsHolding<float>()) {
        payload.material.metallic = metallicValue.UncheckedGet<float>();
    }

    const VtValue roughnessValue = sceneDelegate->Get(id, roughnessToken);
    if (roughnessValue.IsHolding<float>()) {
        payload.material.roughness = roughnessValue.UncheckedGet<float>();
    }

    const VtValue emissionValue = sceneDelegate->Get(id, emissionToken);
    if (emissionValue.IsHolding<GfVec3f>()) {
        payload.material.emissionColor = ToVec3f(emissionValue.UncheckedGet<GfVec3f>());
        payload.material.emissionStrength = 1.0f;
    }

    return payload;
}

shiro::render::Camera HdShiroSceneBridge::BuildCamera(const HdRenderPassStateSharedPtr& renderPassState) {
    shiro::render::Camera camera;

    const GfMatrix4d viewToWorld = renderPassState->GetWorldToViewMatrix().GetInverse();
    camera.position = ToVec3f(viewToWorld.ExtractTranslation());
    camera.forward = shiro::render::Normalize(ToVec3f(viewToWorld.TransformDir(GfVec3d(0.0, 0.0, -1.0))));
    camera.right = shiro::render::Normalize(ToVec3f(viewToWorld.TransformDir(GfVec3d(1.0, 0.0, 0.0))));
    camera.up = shiro::render::Normalize(ToVec3f(viewToWorld.TransformDir(GfVec3d(0.0, 1.0, 0.0))));

    const GfVec4d viewport = renderPassState->GetViewport();
    camera.aspectRatio = viewport[3] > 0.0 ? static_cast<float>(viewport[2] / viewport[3]) : 1.0f;

    const GfMatrix4d projection = renderPassState->GetProjectionMatrix();
    if (std::fabs(projection[1][1]) > 1.0e-6) {
        camera.verticalFovDegrees =
            static_cast<float>(2.0 * std::atan(1.0 / projection[1][1]) * 180.0 / 3.14159265358979323846);
    }

    return camera;
}

PXR_NAMESPACE_CLOSE_SCOPE

#endif
