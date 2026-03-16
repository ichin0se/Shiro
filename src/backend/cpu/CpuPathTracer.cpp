#include "shiro/backend/cpu/CpuPathTracer.h"

#include "shiro/render/EnvironmentMap.h"
#include "shiro/render/OpenQmcSampler.h"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <limits>
#include <thread>
#include <vector>

#if SHIRO_HAVE_EMBREE
#include <rtcore.h>
#endif

namespace shiro::backend::cpu {

using namespace shiro::render;

struct AccelerationScene {
#if SHIRO_HAVE_EMBREE
    RTCDevice device = nullptr;
    RTCScene scene = nullptr;
#endif
    std::vector<size_t> meshIndicesByGeometry;

    AccelerationScene() = default;
    AccelerationScene(const AccelerationScene&) = delete;
    AccelerationScene& operator=(const AccelerationScene&) = delete;

    ~AccelerationScene() {
#if SHIRO_HAVE_EMBREE
        if (scene) {
            rtcReleaseScene(scene);
        }
        if (device) {
            rtcReleaseDevice(device);
        }
#endif
    }
};

namespace {

constexpr float kPi = 3.14159265358979323846f;
constexpr float kInvPi = 1.0f / kPi;
constexpr float kInvFourPi = 1.0f / (4.0f * kPi);
constexpr float kRayEpsilon = 1.0e-4f;
constexpr float kDirectSampleClamp = 24.0f;
constexpr float kThroughputClamp = 12.0f;

bool IsCancelled(const std::atomic<bool>* cancel) {
    return cancel && cancel->load(std::memory_order_relaxed);
}

struct Ray {
    Vec3f origin;
    Vec3f direction;
    float tMin = kRayEpsilon;
    float tMax = FLT_MAX;
};

struct Hit {
    bool hasHit = false;
    float distance = FLT_MAX;
    Vec3f position;
    Vec3f geometricNormal;
    Vec3f shadingNormal;
    uint32_t materialIndex = 0;
};

struct SampleResult {
    Vec3f radiance{};
    Vec3f albedo{};
    Vec3f normal{};
    float depth = std::numeric_limits<float>::infinity();
};

Vec3f TriangleGeometricNormal(const TriangleMesh& mesh, uint32_t primitiveIndex) {
    const size_t indexOffset = static_cast<size_t>(primitiveIndex) * 3u;
    if (indexOffset + 2u >= mesh.indices.size()) {
        return {0.0f, 1.0f, 0.0f};
    }

    const uint32_t i0 = mesh.indices[indexOffset + 0u];
    const uint32_t i1 = mesh.indices[indexOffset + 1u];
    const uint32_t i2 = mesh.indices[indexOffset + 2u];
    if (i0 >= mesh.positions.size() || i1 >= mesh.positions.size() || i2 >= mesh.positions.size()) {
        return {0.0f, 1.0f, 0.0f};
    }

    return Normalize(Cross(mesh.positions[i1] - mesh.positions[i0], mesh.positions[i2] - mesh.positions[i0]));
}

Vec3f TriangleShadingNormal(
    const TriangleMesh& mesh,
    uint32_t primitiveIndex,
    float b0,
    float b1,
    float b2) {
    const size_t indexOffset = static_cast<size_t>(primitiveIndex) * 3u;
    if (indexOffset + 2u >= mesh.indices.size()) {
        return {0.0f, 1.0f, 0.0f};
    }

    const uint32_t i0 = mesh.indices[indexOffset + 0u];
    const uint32_t i1 = mesh.indices[indexOffset + 1u];
    const uint32_t i2 = mesh.indices[indexOffset + 2u];
    if (mesh.normals.size() == mesh.positions.size()
        && i0 < mesh.normals.size()
        && i1 < mesh.normals.size()
        && i2 < mesh.normals.size()) {
        const Vec3f interpolated =
            mesh.normals[i0] * b0 + mesh.normals[i1] * b1 + mesh.normals[i2] * b2;
        if (Length(interpolated) > 0.0f) {
            return Normalize(interpolated);
        }
    }

    return TriangleGeometricNormal(mesh, primitiveIndex);
}

Vec3f BuildOrthonormalX(const Vec3f& normal) {
    const Vec3f axis = std::fabs(normal.z) < 0.999f ? Vec3f{0.0f, 0.0f, 1.0f} : Vec3f{0.0f, 1.0f, 0.0f};
    return Normalize(Cross(axis, normal));
}

Vec3f CosineSampleHemisphere(const Vec3f& normal, const Vec2f& sample) {
    const float phi = 2.0f * kPi * sample.x;
    const float radius = std::sqrt(sample.y);
    const float x = radius * std::cos(phi);
    const float y = radius * std::sin(phi);
    const float z = std::sqrt(std::max(0.0f, 1.0f - sample.y));

    const Vec3f tangent = BuildOrthonormalX(normal);
    const Vec3f bitangent = Cross(normal, tangent);
    return Normalize(tangent * x + bitangent * y + normal * z);
}

Vec3f GgxSampleHalfVector(const Vec3f& normal, const Vec2f& sample, float roughness) {
    const float alpha = std::max(0.02f, roughness * roughness);
    const float alpha2 = alpha * alpha;
    const float phi = 2.0f * kPi * sample.x;
    const float cosTheta = std::sqrt(
        std::max(0.0f, (1.0f - sample.y) / std::max(1.0e-6f, 1.0f + (alpha2 - 1.0f) * sample.y)));
    const float sinTheta = std::sqrt(std::max(0.0f, 1.0f - cosTheta * cosTheta));
    const float x = sinTheta * std::cos(phi);
    const float y = sinTheta * std::sin(phi);
    const float z = cosTheta;

    const Vec3f tangent = BuildOrthonormalX(normal);
    const Vec3f bitangent = Cross(normal, tangent);
    return Normalize(tangent * x + bitangent * y + normal * z);
}

Vec3f UniformSampleSphere(const Vec2f& sample) {
    const float y = 1.0f - 2.0f * sample.y;
    const float radius = std::sqrt(std::max(0.0f, 1.0f - y * y));
    const float phi = 2.0f * kPi * sample.x;
    return {
        std::sin(phi) * radius,
        y,
        std::cos(phi) * radius,
    };
}

Vec3f WorldToLight(const DomeLight& light, const Vec3f& direction) {
    return Normalize({
        Dot(direction, light.right),
        Dot(direction, light.up),
        Dot(direction, light.forward),
    });
}

Vec3f LightToWorld(const DomeLight& light, const Vec3f& direction) {
    return Normalize(light.right * direction.x + light.up * direction.y + light.forward * direction.z);
}

Vec3f EvaluateDomeLight(const DomeLight& light, const Vec3f& direction) {
    if (light.environment) {
        return light.environment->Evaluate(WorldToLight(light, direction)) * light.radiance;
    }
    return light.radiance;
}

Vec3f EnvironmentRadiance(const Scene& scene, const Vec3f& direction) {
    if (!scene.domeLights.empty()) {
        Vec3f radiance{};
        for (const DomeLight& light : scene.domeLights) {
            radiance = radiance + EvaluateDomeLight(light, direction);
        }
        return radiance;
    }

    const float t = Clamp(direction.y * 0.5f + 0.5f, 0.0f, 1.0f);
    return Lerp(scene.environmentBottom, scene.environmentTop, t);
}

float EvaluateDomeLightPdf(const DomeLight& light, const Vec3f& direction) {
    if (light.environment) {
        return light.environment->Pdf(WorldToLight(light, direction));
    }
    return kInvFourPi;
}

bool SampleDomeLight(
    const DomeLight& light,
    const Vec2f& sample,
    Vec3f* direction,
    Vec3f* radiance,
    float* pdf) {
    if (!direction || !radiance || !pdf) {
        return false;
    }

    Vec3f localDirection{};
    float localPdf = 0.0f;
    Vec3f localRadiance{};

    if (light.environment) {
        localRadiance = light.environment->Sample(sample, &localDirection, &localPdf);
    } else {
        localDirection = UniformSampleSphere(sample);
        localPdf = kInvFourPi;
        localRadiance = {1.0f, 1.0f, 1.0f};
    }

    if (localPdf <= 0.0f) {
        return false;
    }

    *direction = LightToWorld(light, localDirection);
    *radiance = localRadiance * light.radiance;
    *pdf = localPdf;
    return true;
}

bool IntersectTriangle(
    const Ray& ray,
    const Vec3f& p0,
    const Vec3f& p1,
    const Vec3f& p2,
    float* outDistance,
    float* outU = nullptr,
    float* outV = nullptr) {
    const Vec3f edge1 = p1 - p0;
    const Vec3f edge2 = p2 - p0;
    const Vec3f pvec = Cross(ray.direction, edge2);
    const float det = Dot(edge1, pvec);

    if (std::fabs(det) < 1.0e-8f) {
        return false;
    }

    const float inverseDet = 1.0f / det;
    const Vec3f tvec = ray.origin - p0;
    const float u = Dot(tvec, pvec) * inverseDet;
    if (u < 0.0f || u > 1.0f) {
        return false;
    }

    const Vec3f qvec = Cross(tvec, edge1);
    const float v = Dot(ray.direction, qvec) * inverseDet;
    if (v < 0.0f || u + v > 1.0f) {
        return false;
    }

    const float t = Dot(edge2, qvec) * inverseDet;
    if (t < ray.tMin || t > ray.tMax) {
        return false;
    }

    *outDistance = t;
    if (outU) {
        *outU = u;
    }
    if (outV) {
        *outV = v;
    }
    return true;
}

bool IntersectSceneBruteForce(const Scene& scene, const Ray& ray, Hit* outHit) {
    Hit bestHit;

    for (const TriangleMesh& mesh : scene.meshes) {
        for (size_t index = 0; index + 2 < mesh.indices.size(); index += 3) {
            const Vec3f& p0 = mesh.positions[mesh.indices[index + 0]];
            const Vec3f& p1 = mesh.positions[mesh.indices[index + 1]];
            const Vec3f& p2 = mesh.positions[mesh.indices[index + 2]];

            float distance = FLT_MAX;
            float u = 0.0f;
            float v = 0.0f;
            if (!IntersectTriangle(ray, p0, p1, p2, &distance, &u, &v) || distance >= bestHit.distance) {
                continue;
            }

            bestHit.hasHit = true;
            bestHit.distance = distance;
            bestHit.position = ray.origin + ray.direction * distance;
            bestHit.geometricNormal = Normalize(Cross(p1 - p0, p2 - p0));
            bestHit.shadingNormal =
                TriangleShadingNormal(mesh, static_cast<uint32_t>(index / 3u), 1.0f - u - v, u, v);
            bestHit.materialIndex = mesh.materialIndex;
        }
    }

    if (!bestHit.hasHit) {
        return false;
    }

    *outHit = bestHit;
    return true;
}

bool OccludedBruteForce(const Scene& scene, const Vec3f& origin, const Vec3f& direction) {
    Hit occlusionHit;
    Ray ray;
    ray.origin = origin;
    ray.direction = direction;
    return IntersectSceneBruteForce(scene, ray, &occlusionHit);
}

#if SHIRO_HAVE_EMBREE
std::shared_ptr<const AccelerationScene> BuildAccelerationScene(
    const Scene& scene,
    const RenderSettings& settings) {
    auto acceleration = std::make_shared<AccelerationScene>();

    std::string deviceConfig;
    if (settings.threadLimit > 0) {
        deviceConfig = "threads=" + std::to_string(settings.threadLimit);
    }

    acceleration->device = rtcNewDevice(deviceConfig.empty() ? nullptr : deviceConfig.c_str());
    if (!acceleration->device) {
        return nullptr;
    }

    acceleration->scene = rtcNewScene(acceleration->device);
    if (!acceleration->scene) {
        return nullptr;
    }

    rtcSetSceneBuildQuality(acceleration->scene, RTC_BUILD_QUALITY_LOW);

    for (size_t meshIndex = 0; meshIndex < scene.meshes.size(); ++meshIndex) {
        const TriangleMesh& mesh = scene.meshes[meshIndex];
        if (mesh.positions.size() < 3 || mesh.indices.size() < 3 || mesh.indices.size() % 3 != 0) {
            continue;
        }

        RTCGeometry geometry = rtcNewGeometry(acceleration->device, RTC_GEOMETRY_TYPE_TRIANGLE);
        if (!geometry) {
            continue;
        }

        rtcSetSharedGeometryBuffer(
            geometry,
            RTC_BUFFER_TYPE_VERTEX,
            0,
            RTC_FORMAT_FLOAT3,
            mesh.positions.data(),
            0,
            sizeof(Vec3f),
            mesh.positions.size());
        rtcSetSharedGeometryBuffer(
            geometry,
            RTC_BUFFER_TYPE_INDEX,
            0,
            RTC_FORMAT_UINT3,
            mesh.indices.data(),
            0,
            sizeof(uint32_t) * 3u,
            mesh.indices.size() / 3u);
        rtcCommitGeometry(geometry);

        const unsigned geometryId = rtcAttachGeometry(acceleration->scene, geometry);
        rtcReleaseGeometry(geometry);
        if (geometryId == RTC_INVALID_GEOMETRY_ID) {
            continue;
        }

        if (acceleration->meshIndicesByGeometry.size() <= geometryId) {
            acceleration->meshIndicesByGeometry.resize(static_cast<size_t>(geometryId) + 1u, std::numeric_limits<size_t>::max());
        }
        acceleration->meshIndicesByGeometry[geometryId] = meshIndex;
    }

    rtcCommitScene(acceleration->scene);
    if (rtcGetDeviceError(acceleration->device) != RTC_ERROR_NONE) {
        return nullptr;
    }

    return acceleration;
}

bool IntersectSceneEmbree(
    const Scene& scene,
    const AccelerationScene& acceleration,
    const Ray& ray,
    Hit* outHit) {
    RTCIntersectContext context;
    rtcInitIntersectContext(&context);

    RTCRayHit rayHit{};
    rayHit.ray.org_x = ray.origin.x;
    rayHit.ray.org_y = ray.origin.y;
    rayHit.ray.org_z = ray.origin.z;
    rayHit.ray.dir_x = ray.direction.x;
    rayHit.ray.dir_y = ray.direction.y;
    rayHit.ray.dir_z = ray.direction.z;
    rayHit.ray.tnear = ray.tMin;
    rayHit.ray.tfar = ray.tMax;
    rayHit.ray.mask = 0xffffffffu;
    rayHit.ray.flags = 0;
    rayHit.hit.geomID = RTC_INVALID_GEOMETRY_ID;
    rayHit.hit.instID[0] = RTC_INVALID_GEOMETRY_ID;

    rtcIntersect1(acceleration.scene, &context, &rayHit);
    if (rayHit.hit.geomID == RTC_INVALID_GEOMETRY_ID) {
        return false;
    }

    const unsigned geometryId = rayHit.hit.geomID;
    if (geometryId >= acceleration.meshIndicesByGeometry.size()) {
        return false;
    }

    const size_t meshIndex = acceleration.meshIndicesByGeometry[geometryId];
    if (meshIndex == std::numeric_limits<size_t>::max() || meshIndex >= scene.meshes.size()) {
        return false;
    }

    const TriangleMesh& mesh = scene.meshes[meshIndex];
    Hit hit;
    hit.hasHit = true;
    hit.distance = rayHit.ray.tfar;
    hit.position = ray.origin + ray.direction * rayHit.ray.tfar;
    hit.geometricNormal = Normalize(Vec3f{rayHit.hit.Ng_x, rayHit.hit.Ng_y, rayHit.hit.Ng_z});
    if (Length(hit.geometricNormal) <= 0.0f) {
        hit.geometricNormal = TriangleGeometricNormal(mesh, rayHit.hit.primID);
    }
    hit.shadingNormal =
        TriangleShadingNormal(mesh, rayHit.hit.primID, 1.0f - rayHit.hit.u - rayHit.hit.v, rayHit.hit.u, rayHit.hit.v);
    if (Length(hit.shadingNormal) <= 0.0f) {
        hit.shadingNormal = hit.geometricNormal;
    }
    hit.materialIndex = mesh.materialIndex;
    *outHit = hit;
    return true;
}

bool OccludedEmbree(
    const AccelerationScene& acceleration,
    const Vec3f& origin,
    const Vec3f& direction) {
    RTCIntersectContext context;
    rtcInitIntersectContext(&context);

    RTCRay ray{};
    ray.org_x = origin.x;
    ray.org_y = origin.y;
    ray.org_z = origin.z;
    ray.dir_x = direction.x;
    ray.dir_y = direction.y;
    ray.dir_z = direction.z;
    ray.tnear = kRayEpsilon;
    ray.tfar = FLT_MAX;
    ray.mask = 0xffffffffu;
    ray.flags = 0;

    rtcOccluded1(acceleration.scene, &context, &ray);
    return ray.tfar < 0.0f;
}
#endif

bool IntersectScene(
    const Scene& scene,
    const AccelerationScene* acceleration,
    const Ray& ray,
    Hit* outHit) {
#if SHIRO_HAVE_EMBREE
    if (acceleration && acceleration->scene) {
        return IntersectSceneEmbree(scene, *acceleration, ray, outHit);
    }
#endif
    return IntersectSceneBruteForce(scene, ray, outHit);
}

bool Occluded(
    const Scene& scene,
    const AccelerationScene* acceleration,
    const Vec3f& origin,
    const Vec3f& direction) {
#if SHIRO_HAVE_EMBREE
    if (acceleration && acceleration->scene) {
        return OccludedEmbree(*acceleration, origin, direction);
    }
#endif
    return OccludedBruteForce(scene, origin, direction);
}

float Square(float value) {
    return value * value;
}

Vec3f SafeNormalized(const Vec3f& value, const Vec3f& fallback) {
    const float length = Length(value);
    return length > 0.0f ? value / length : fallback;
}

Vec3f Exp(const Vec3f& value) {
    return {std::exp(value.x), std::exp(value.y), std::exp(value.z)};
}

float AdjustedRoughness(float roughness, float anisotropy, float rotation) {
    const float clampedAnisotropy = Clamp(anisotropy, -0.95f, 0.95f);
    const float rotationPhase = std::cos(rotation * 2.0f * kPi);
    const float adjusted = roughness * (1.0f - 0.35f * clampedAnisotropy * rotationPhase);
    return Clamp(adjusted, 0.02f, 1.0f);
}

Vec3f ProjectedTangent(const PbrMaterial& material, const Vec3f& normal) {
    if (material.hasTangentOverride) {
        const Vec3f tangent = material.tangentOverride - normal * Dot(material.tangentOverride, normal);
        if (Length(tangent) > 0.0f) {
            return Normalize(tangent);
        }
    }
    return BuildOrthonormalX(normal);
}

float AnisotropyPhase(
    const Vec3f& normal,
    const Vec3f& tangent,
    const Vec3f& viewDirection,
    const Vec3f& lightDirection,
    float rotation) {
    const Vec3f tangentDirection = SafeNormalized(tangent - normal * Dot(tangent, normal), BuildOrthonormalX(normal));
    const Vec3f bitangent = Normalize(Cross(normal, tangentDirection));
    const Vec3f halfVector = SafeNormalized(viewDirection + lightDirection, normal);
    const Vec3f halfProjected = halfVector - normal * Dot(normal, halfVector);
    if (Length(halfProjected) <= 1.0e-6f) {
        return std::cos(rotation * 2.0f * kPi);
    }

    const Vec3f halfDirection = Normalize(halfProjected);
    const float angle = std::atan2(Dot(halfDirection, bitangent), Dot(halfDirection, tangentDirection));
    return std::cos(angle - rotation * 2.0f * kPi);
}

float AdjustedRoughness(
    float roughness,
    float anisotropy,
    float rotation,
    const Vec3f& normal,
    const Vec3f& tangent,
    const Vec3f& viewDirection,
    const Vec3f& lightDirection) {
    const float clampedAnisotropy = Clamp(anisotropy, -0.95f, 0.95f);
    const float orientationPhase = AnisotropyPhase(normal, tangent, viewDirection, lightDirection, rotation);
    const float adjusted = roughness * (1.0f - 0.35f * clampedAnisotropy * orientationPhase);
    return Clamp(adjusted, 0.02f, 1.0f);
}

float DielectricF0Scalar(float ior) {
    const float safeIor = std::max(ior, 1.0f);
    const float ratio = (safeIor - 1.0f) / (safeIor + 1.0f);
    return Square(ratio);
}

Vec3f DiffuseColor(const PbrMaterial& material) {
    const float metallic = Clamp(material.metallic, 0.0f, 1.0f);
    const float transmission = Clamp(material.transmission, 0.0f, 1.0f);
    const float subsurface = Clamp(material.subsurface, 0.0f, 1.0f);
    const float radiusBlend = Clamp(MaxComponent(material.subsurfaceRadius) / 3.0f, 0.0f, 1.0f);
    const Vec3f subsurfaceTint = Lerp(material.baseColor, material.subsurfaceColor, radiusBlend);
    const Vec3f diffuseBase = Lerp(material.baseColor, subsurfaceTint, subsurface);
    return diffuseBase * (Clamp(material.baseWeight, 0.0f, 1.0f) * (1.0f - metallic) * (1.0f - transmission));
}

float BaseLayerRoughness(const PbrMaterial& material) {
    return Clamp(
        material.roughness + material.coatAffectRoughness * material.coatWeight * material.coatRoughness,
        0.0f,
        1.0f);
}

Vec3f SpecularF0(const PbrMaterial& material) {
    const float metallic = Clamp(material.metallic, 0.0f, 1.0f);
    const Vec3f dielectricF0 =
        material.specularColor
        * (Clamp(material.specularWeight, 0.0f, 1.0f) * DielectricF0Scalar(material.ior));
    return Lerp(dielectricF0, material.baseColor, metallic);
}

Vec3f CoatF0(const PbrMaterial& material) {
    return material.coatColor * (Clamp(material.coatWeight, 0.0f, 1.0f) * DielectricF0Scalar(material.coatIor));
}

float TransmissionRoughness(const PbrMaterial& material) {
    return Clamp(material.roughness + material.transmissionExtraRoughness, 0.0f, 1.0f);
}

Vec3f ThinFilmColor(float thickness, float ior, float vDotH) {
    if (thickness <= 0.0f) {
        return {1.0f, 1.0f, 1.0f};
    }

    const float safeIor = std::max(ior, 1.0f);
    const float opticalPath = 2.0f * safeIor * thickness * Clamp(vDotH, 0.05f, 1.0f);
    const auto spectralBand = [&](float wavelength) {
        const float phase = 4.0f * kPi * opticalPath / std::max(wavelength, 1.0f);
        return 0.5f + 0.5f * std::cos(phase);
    };

    const Vec3f iridescence{
        spectralBand(650.0f),
        spectralBand(510.0f),
        spectralBand(475.0f),
    };
    const float strength = Clamp(thickness / 1200.0f, 0.0f, 1.0f);
    return Lerp(Vec3f{1.0f, 1.0f, 1.0f}, (Vec3f{1.0f, 1.0f, 1.0f} + iridescence) * 0.5f, strength);
}

Vec3f ApplyThinFilm(
    const Vec3f& lobe,
    const PbrMaterial& material,
    const Vec3f& normal,
    const Vec3f& viewDirection,
    const Vec3f& lightDirection) {
    if (material.thinFilmThickness <= 0.0f) {
        return lobe;
    }

    const Vec3f halfVector = SafeNormalized(viewDirection + lightDirection, normal);
    const float vDotH = Clamp(Dot(viewDirection, halfVector), 0.0f, 1.0f);
    return lobe * ThinFilmColor(material.thinFilmThickness, material.thinFilmIor, vDotH);
}

Vec3f ResolveShadingNormal(const PbrMaterial& material, const Vec3f& fallback, bool coatLayer) {
    const bool hasOverride = coatLayer ? material.hasCoatNormalOverride : material.hasNormalOverride;
    if (!hasOverride) {
        return fallback;
    }

    const Vec3f candidate = coatLayer ? material.coatNormalOverride : material.normalOverride;
    if (Length(candidate) <= 0.0f) {
        return fallback;
    }

    Vec3f normal = Normalize(candidate);
    if (Dot(normal, fallback) < 0.0f) {
        normal = normal * -1.0f;
    }
    return normal;
}

float GgxDistribution(float nDotH, float roughness) {
    const float alpha = std::max(0.02f, roughness * roughness);
    const float alpha2 = alpha * alpha;
    const float denom = std::max(1.0e-6f, Square(nDotH * nDotH * (alpha2 - 1.0f) + 1.0f));
    return alpha2 / (kPi * denom);
}

float GgxSmithVisibility(float nDotV, float nDotL, float roughness) {
    const float alpha = std::max(0.02f, roughness * roughness);
    const float alpha2 = alpha * alpha;
    const auto lambda = [&](float nDot) {
        const float clamped = std::max(nDot, 1.0e-6f);
        return (-1.0f + std::sqrt(1.0f + alpha2 * (1.0f - clamped * clamped) / (clamped * clamped))) * 0.5f;
    };
    return 1.0f / (1.0f + lambda(nDotV) + lambda(nDotL));
}

Vec3f FresnelSchlick(const Vec3f& f0, float vDotH) {
    const float clamped = Clamp(1.0f - vDotH, 0.0f, 1.0f);
    const float factor = clamped * clamped * clamped * clamped * clamped;
    return f0 + (Vec3f{1.0f, 1.0f, 1.0f} - f0) * factor;
}

Vec3f OneMinusClamped(const Vec3f& value) {
    return {
        Clamp(1.0f - value.x, 0.0f, 1.0f),
        Clamp(1.0f - value.y, 0.0f, 1.0f),
        Clamp(1.0f - value.z, 0.0f, 1.0f),
    };
}

Vec3f CoatUnderlayerTint(const PbrMaterial& material) {
    const float coatAffect = Clamp(material.coatAffectColor * material.coatWeight, 0.0f, 1.0f);
    return Lerp(Vec3f{1.0f, 1.0f, 1.0f}, Clamp(material.coatColor, 0.0f, 1.0f), coatAffect);
}

Vec3f DirectionalLayerTransmittance(const Vec3f& f0, float nDotV, float nDotL) {
    const Vec3f fresnelV = FresnelSchlick(f0, Clamp(nDotV, 0.0f, 1.0f));
    const Vec3f fresnelL = FresnelSchlick(f0, Clamp(nDotL, 0.0f, 1.0f));
    return OneMinusClamped((fresnelV + fresnelL) * 0.5f);
}

Vec3f ViewLayerTransmittance(const Vec3f& f0, float nDotV) {
    return OneMinusClamped(FresnelSchlick(f0, Clamp(nDotV, 0.0f, 1.0f)));
}

Vec3f TransmissionInterfaceTransmittance(const Vec3f& f0, float cosThetaA, float cosThetaB) {
    const Vec3f fresnelA = FresnelSchlick(f0, Clamp(std::fabs(cosThetaA), 0.0f, 1.0f));
    const Vec3f fresnelB = FresnelSchlick(f0, Clamp(std::fabs(cosThetaB), 0.0f, 1.0f));
    return OneMinusClamped((fresnelA + fresnelB) * 0.5f);
}

Vec3f CoatUnderlayerTransmittance(
    const PbrMaterial& material,
    const Vec3f& coatNormal,
    const Vec3f& viewDirection,
    const Vec3f& lightDirection) {
    return CoatUnderlayerTint(material)
        * DirectionalLayerTransmittance(
            CoatF0(material),
            Dot(coatNormal, viewDirection),
            Dot(coatNormal, lightDirection));
}

Vec3f CoatViewTransmittance(
    const PbrMaterial& material,
    const Vec3f& coatNormal,
    const Vec3f& viewDirection) {
    return CoatUnderlayerTint(material)
        * ViewLayerTransmittance(CoatF0(material), Dot(coatNormal, viewDirection));
}

Vec3f CoatTransmissionScale(
    const PbrMaterial& material,
    const Vec3f& coatNormal,
    const Vec3f& viewDirection,
    const Vec3f& lightDirection) {
    return CoatUnderlayerTint(material)
        * TransmissionInterfaceTransmittance(
            CoatF0(material),
            Dot(coatNormal, viewDirection),
            Dot(coatNormal, lightDirection));
}

Vec3f EvaluateSpecularLobe(
    const Vec3f& normal,
    const Vec3f& viewDirection,
    const Vec3f& lightDirection,
    const Vec3f& f0,
    float roughness) {
    const float nDotV = Clamp(Dot(normal, viewDirection), 0.0f, 1.0f);
    const float nDotL = Clamp(Dot(normal, lightDirection), 0.0f, 1.0f);
    if (nDotV <= 0.0f || nDotL <= 0.0f) {
        return {};
    }

    const Vec3f halfVector = SafeNormalized(viewDirection + lightDirection, normal);
    const float nDotH = Clamp(Dot(normal, halfVector), 0.0f, 1.0f);
    const float vDotH = Clamp(Dot(viewDirection, halfVector), 0.0f, 1.0f);
    const float distribution = GgxDistribution(nDotH, roughness);
    const float visibility = GgxSmithVisibility(nDotV, nDotL, roughness);
    const Vec3f fresnel = FresnelSchlick(f0, vDotH);
    return fresnel * (distribution * visibility / std::max(4.0f * nDotV * nDotL, 1.0e-6f));
}

float EvaluateSpecularPdf(
    const Vec3f& normal,
    const Vec3f& viewDirection,
    const Vec3f& lightDirection,
    float roughness) {
    const float nDotV = Clamp(Dot(normal, viewDirection), 0.0f, 1.0f);
    const float nDotL = Clamp(Dot(normal, lightDirection), 0.0f, 1.0f);
    if (nDotV <= 0.0f || nDotL <= 0.0f) {
        return 0.0f;
    }

    const Vec3f halfVector = SafeNormalized(viewDirection + lightDirection, normal);
    const float nDotH = Clamp(Dot(normal, halfVector), 0.0f, 1.0f);
    const float vDotH = Clamp(Dot(viewDirection, halfVector), 1.0e-6f, 1.0f);
    return GgxDistribution(nDotH, roughness) * nDotH / std::max(4.0f * vDotH, 1.0e-6f);
}

Vec3f TransmissionTint(const PbrMaterial& material);

Vec3f EvaluateTransmissionLobe(
    const PbrMaterial& material,
    const Vec3f& normal,
    const Vec3f& coatNormal,
    const Vec3f& viewDirection,
    const Vec3f& lightDirection,
    float eta,
    float roughness) {
    const float cosWo = Dot(normal, viewDirection);
    const float cosWi = Dot(normal, lightDirection);
    const float absCosWo = std::fabs(cosWo);
    const float absCosWi = std::fabs(cosWi);
    if (absCosWo <= 0.0f || absCosWi <= 0.0f || cosWo * cosWi >= 0.0f) {
        return {};
    }

    Vec3f halfVector = SafeNormalized(viewDirection + lightDirection * eta, normal);
    if (Dot(normal, halfVector) < 0.0f) {
        halfVector = halfVector * -1.0f;
    }

    const float woDotH = Dot(viewDirection, halfVector);
    const float wiDotH = Dot(lightDirection, halfVector);
    if (woDotH * wiDotH >= 0.0f) {
        return {};
    }

    const float nDotH = Clamp(Dot(normal, halfVector), 0.0f, 1.0f);
    const float sqrtDenom = woDotH + eta * wiDotH;
    if (nDotH <= 0.0f || std::fabs(sqrtDenom) <= 1.0e-6f) {
        return {};
    }

    const float distribution = GgxDistribution(nDotH, roughness);
    const float visibility = GgxSmithVisibility(absCosWo, absCosWi, roughness);
    const Vec3f fresnel =
        OneMinusClamped(FresnelSchlick(SpecularF0(material), Clamp(std::fabs(woDotH), 0.0f, 1.0f)));
    const Vec3f transmissionScale = TransmissionTint(material)
        * CoatTransmissionScale(material, coatNormal, viewDirection, lightDirection)
        * (Clamp(material.transmission, 0.0f, 1.0f) * (1.0f - Clamp(material.metallic, 0.0f, 1.0f)));
    const float factor = std::fabs(
        distribution * visibility * std::fabs(wiDotH) * std::fabs(woDotH)
        / std::max(absCosWo * absCosWi * Square(sqrtDenom), 1.0e-6f));
    return transmissionScale * fresnel * factor;
}

float EvaluateTransmissionPdf(
    const Vec3f& normal,
    const Vec3f& viewDirection,
    const Vec3f& lightDirection,
    float eta,
    float roughness) {
    const float cosWo = Dot(normal, viewDirection);
    const float cosWi = Dot(normal, lightDirection);
    if (std::fabs(cosWo) <= 0.0f || std::fabs(cosWi) <= 0.0f || cosWo * cosWi >= 0.0f) {
        return 0.0f;
    }

    Vec3f halfVector = SafeNormalized(viewDirection + lightDirection * eta, normal);
    if (Dot(normal, halfVector) < 0.0f) {
        halfVector = halfVector * -1.0f;
    }

    const float woDotH = Dot(viewDirection, halfVector);
    const float wiDotH = Dot(lightDirection, halfVector);
    const float sqrtDenom = woDotH + eta * wiDotH;
    if (woDotH * wiDotH >= 0.0f || std::fabs(sqrtDenom) <= 1.0e-6f) {
        return 0.0f;
    }

    const float nDotH = Clamp(Dot(normal, halfVector), 0.0f, 1.0f);
    const float pdfWh = GgxDistribution(nDotH, roughness) * nDotH;
    const float dwhDwi = std::fabs((eta * eta * wiDotH) / Square(sqrtDenom));
    return pdfWh * dwhDwi;
}

float DiffuseSampleWeight(const PbrMaterial& material);
float BaseSpecularSampleWeight(const PbrMaterial& material);
float CoatSampleWeight(const PbrMaterial& material);

float PowerHeuristic(float lhsPdf, float rhsPdf) {
    const float lhs2 = lhsPdf * lhsPdf;
    const float rhs2 = rhsPdf * rhsPdf;
    return lhs2 / std::max(lhs2 + rhs2, 1.0e-6f);
}

Vec3f ClampMaxComponentValue(const Vec3f& value, float maxComponent) {
    const float current = MaxComponent(value);
    if (current <= maxComponent || current <= 0.0f) {
        return value;
    }
    return value * (maxComponent / current);
}

float EvaluateBsdfPdf(
    const PbrMaterial& material,
    const Vec3f& normal,
    const Vec3f& coatNormal,
    const Vec3f& viewDirection,
    const Vec3f& lightDirection) {
    const float nDotL = Clamp(Dot(normal, lightDirection), 0.0f, 1.0f);
    if (nDotL <= 0.0f) {
        return 0.0f;
    }

    const float diffuseLobeWeight = DiffuseSampleWeight(material);
    const float baseSpecularLobeWeight = BaseSpecularSampleWeight(material);
    const float coatLobeWeight = CoatSampleWeight(material);
    const float reflectiveWeight = diffuseLobeWeight + baseSpecularLobeWeight + coatLobeWeight;
    if (reflectiveWeight <= 0.0f) {
        return 0.0f;
    }

    const float diffuseProbability = diffuseLobeWeight / reflectiveWeight;
    const float baseSpecularProbability = baseSpecularLobeWeight / reflectiveWeight;
    const float coatProbability = coatLobeWeight / reflectiveWeight;
    const float specularRoughness = AdjustedRoughness(
        BaseLayerRoughness(material),
        material.specularAnisotropy,
        material.specularRotation,
        normal,
        ProjectedTangent(material, normal),
        viewDirection,
        lightDirection);
    const float coatRoughness = AdjustedRoughness(
        material.coatRoughness,
        material.coatAnisotropy,
        material.coatRotation,
        coatNormal,
        ProjectedTangent(material, coatNormal),
        viewDirection,
        lightDirection);
    return diffuseProbability * nDotL * kInvPi
        + baseSpecularProbability * EvaluateSpecularPdf(normal, viewDirection, lightDirection, specularRoughness)
        + coatProbability * EvaluateSpecularPdf(coatNormal, viewDirection, lightDirection, coatRoughness);
}

Vec3f EvaluateBsdf(
    const PbrMaterial& material,
    const Vec3f& normal,
    const Vec3f& coatNormal,
    const Vec3f& viewDirection,
    const Vec3f& lightDirection) {
    const float nDotV = Clamp(Dot(normal, viewDirection), 0.0f, 1.0f);
    const float nDotL = Clamp(Dot(normal, lightDirection), 0.0f, 1.0f);
    if (nDotV <= 0.0f || nDotL <= 0.0f) {
        return {};
    }

    const Vec3f baseF0 = SpecularF0(material);
    const Vec3f coatF0 = CoatF0(material);
    const Vec3f underCoat = CoatUnderlayerTransmittance(material, coatNormal, viewDirection, lightDirection);
    const Vec3f underBase = DirectionalLayerTransmittance(baseF0, nDotV, nDotL);
    const Vec3f tangent = ProjectedTangent(material, normal);
    const Vec3f coatTangent = ProjectedTangent(material, coatNormal);
    const Vec3f viewProjected = Normalize(viewDirection - normal * nDotV);
    const Vec3f lightProjected = Normalize(lightDirection - normal * nDotL);
    const float cosPhi = Length(viewProjected) > 0.0f && Length(lightProjected) > 0.0f
        ? Clamp(Dot(viewProjected, lightProjected), -1.0f, 1.0f)
        : 0.0f;
    const float sigma = Clamp(material.diffuseRoughness, 0.0f, 1.0f);
    const float sigma2 = sigma * sigma;
    const float orenA = 1.0f - (sigma2 / (2.0f * (sigma2 + 0.33f)));
    const float orenB = 0.45f * sigma2 / (sigma2 + 0.09f);
    const float sinAlpha = std::max(
        std::sqrt(std::max(0.0f, 1.0f - nDotV * nDotV)),
        std::sqrt(std::max(0.0f, 1.0f - nDotL * nDotL)));
    const float tanBeta = std::min(
        std::sqrt(std::max(0.0f, 1.0f - nDotV * nDotV)) / std::max(nDotV, 1.0e-6f),
        std::sqrt(std::max(0.0f, 1.0f - nDotL * nDotL)) / std::max(nDotL, 1.0e-6f));
    const float wrap = Clamp(
        material.subsurface
            * material.subsurfaceScale
            * (0.35f + 0.15f * Clamp(material.subsurfaceAnisotropy, -1.0f, 1.0f)
                + 0.1f * Clamp(MaxComponent(material.subsurfaceRadius) / 3.0f, 0.0f, 1.0f)),
        0.0f,
        0.9f);
    const float wrappedNDotL = Clamp((nDotL + wrap) / (1.0f + wrap), 0.0f, 1.0f);
    const float diffuseTerm = (orenA + orenB * std::max(0.0f, cosPhi) * sinAlpha * tanBeta)
        * (wrappedNDotL / std::max(nDotL, 1.0e-6f));
    const Vec3f diffuse = DiffuseColor(material) * underCoat * underBase * (kInvPi * diffuseTerm);
    const float specularRoughness = AdjustedRoughness(
        BaseLayerRoughness(material),
        material.specularAnisotropy,
        material.specularRotation,
        normal,
        tangent,
        viewDirection,
        lightDirection);
    const Vec3f specular = underCoat * ApplyThinFilm(
        EvaluateSpecularLobe(normal, viewDirection, lightDirection, baseF0, specularRoughness),
        material,
        normal,
        viewDirection,
        lightDirection);
    const float coatLobeRoughness = AdjustedRoughness(
        material.coatRoughness,
        material.coatAnisotropy,
        material.coatRotation,
        coatNormal,
        coatTangent,
        viewDirection,
        lightDirection);
    const Vec3f coat = ApplyThinFilm(
        EvaluateSpecularLobe(coatNormal, viewDirection, lightDirection, coatF0, coatLobeRoughness),
        material,
        coatNormal,
        viewDirection,
        lightDirection);
    const Vec3f halfVector = SafeNormalized(viewDirection + lightDirection, normal);
    const float sheenWeight = Clamp(material.sheen, 0.0f, 1.0f);
    const float lDotH = Clamp(Dot(lightDirection, halfVector), 0.0f, 1.0f);
    const float sheenFactor =
        std::pow(1.0f - lDotH, 5.0f) * (1.0f - 0.5f * Clamp(material.sheenRoughness, 0.0f, 1.0f));
    const Vec3f sheen = material.sheenColor * sheenWeight * sheenFactor * underCoat * underBase;
    return diffuse + specular + coat + sheen;
}

bool RefractDirection(
    const Vec3f& incident,
    const Vec3f& normal,
    float eta,
    Vec3f* refracted) {
    if (!refracted) {
        return false;
    }

    const float cosTheta = Clamp(-Dot(incident, normal), -1.0f, 1.0f);
    const float sin2Theta = std::max(0.0f, 1.0f - cosTheta * cosTheta);
    const float k = 1.0f - eta * eta * sin2Theta;
    if (k <= 0.0f) {
        return false;
    }

    *refracted = Normalize(incident * eta + normal * (eta * cosTheta - std::sqrt(k)));
    return true;
}

float DiffuseSampleWeight(const PbrMaterial& material) {
    const Vec3f underCoat = CoatUnderlayerTint(material) * OneMinusClamped(CoatF0(material));
    const Vec3f underBase = OneMinusClamped(SpecularF0(material));
    return std::max(0.0f, MaxComponent(DiffuseColor(material) * underCoat * underBase));
}

float BaseSpecularSampleWeight(const PbrMaterial& material) {
    const Vec3f underCoat = CoatUnderlayerTint(material) * OneMinusClamped(CoatF0(material));
    return std::max(0.0f, MaxComponent(underCoat * SpecularF0(material)));
}

float CoatSampleWeight(const PbrMaterial& material) {
    return std::max(0.0f, MaxComponent(CoatF0(material)));
}

float TransmissionSampleWeight(const PbrMaterial& material) {
    const float transmission = Clamp(material.transmission, 0.0f, 1.0f);
    const float metallic = Clamp(material.metallic, 0.0f, 1.0f);
    if (transmission <= 0.0f || metallic >= 0.999f) {
        return 0.0f;
    }

    const Vec3f underCoat = CoatUnderlayerTint(material) * OneMinusClamped(CoatF0(material));
    const Vec3f underBase = OneMinusClamped(SpecularF0(material));
    return std::max(0.0f, MaxComponent(TransmissionTint(material) * underCoat * underBase))
        * transmission * (1.0f - metallic);
}

Vec3f TransmissionTint(const PbrMaterial& material) {
    const float dispersion = Clamp(material.transmissionDispersion, 0.0f, 1.0f);
    return {
        material.transmissionColor.x * (1.0f + 0.15f * dispersion),
        material.transmissionColor.y,
        material.transmissionColor.z * (1.0f + 0.15f * (1.0f - dispersion)),
    };
}

Vec3f MediumExtinction(const PbrMaterial& material) {
    const float depth = std::max(material.transmissionDepth, 1.0e-3f);
    const Vec3f tint = Clamp(material.transmissionColor, 0.0f, 1.0f);
    const Vec3f scatter = Clamp(material.transmissionScatter, 0.0f, 1.0f);
    const float scatterScale = 1.0f - 0.35f * Clamp(material.transmissionScatterAnisotropy, -0.95f, 0.95f);
    return {
        (1.0f - tint.x + scatter.x * scatterScale) / depth,
        (1.0f - tint.y + scatter.y * scatterScale) / depth,
        (1.0f - tint.z + scatter.z * scatterScale) / depth,
    };
}

Vec3f MediumAttenuation(const Vec3f& extinction, float distance) {
    return Exp({-extinction.x * distance, -extinction.y * distance, -extinction.z * distance});
}

Vec3f DirectLighting(
    const Scene& scene,
    const AccelerationScene* acceleration,
    const RenderSettings& settings,
    const Hit& hit,
    const Vec3f& shadingNormal,
    const Vec3f& coatNormal,
    const PbrMaterial& material,
    const Vec3f& viewDirection,
    OpenQmcSampler* sampler,
    uint32_t depth) {
    if (!sampler) {
        return {};
    }

    Vec3f radiance{};
    const Vec3f origin = hit.position + shadingNormal * kRayEpsilon;

    for (const DirectionalLight& light : scene.distantLights) {
        const Vec3f lightDirection = Normalize(light.direction * -1.0f);
        const float nDotL = Clamp(Dot(shadingNormal, lightDirection), 0.0f, 1.0f);
        if (nDotL <= 0.0f || Occluded(scene, acceleration, origin, lightDirection)) {
            continue;
        }

        radiance = radiance
            + ClampMaxComponentValue(
                EvaluateBsdf(material, shadingNormal, coatNormal, viewDirection, lightDirection) * light.radiance * nDotL,
                kDirectSampleClamp);
    }

    const uint32_t domeLightSamples = std::max(1u, settings.domeLightSamples);
    for (size_t lightIndex = 0; lightIndex < scene.domeLights.size(); ++lightIndex) {
        const DomeLight& light = scene.domeLights[lightIndex];
        Vec3f lightContribution{};

        for (uint32_t sampleIndex = 0; sampleIndex < domeLightSamples; ++sampleIndex) {
            Vec3f lightDirection{};
            Vec3f lightRadiance{};
            float lightPdf = 0.0f;
            const Vec2f lightSample = sampler->Next2D(64u + depth * 16u + static_cast<uint32_t>(lightIndex) * 4u + sampleIndex);
            if (!SampleDomeLight(light, lightSample, &lightDirection, &lightRadiance, &lightPdf) || lightPdf <= 0.0f) {
                continue;
            }

            const float nDotL = Clamp(Dot(shadingNormal, lightDirection), 0.0f, 1.0f);
            if (nDotL <= 0.0f || Occluded(scene, acceleration, origin, lightDirection)) {
                continue;
            }

            const float bsdfPdf =
                EvaluateBsdfPdf(material, shadingNormal, coatNormal, viewDirection, lightDirection);
            const float misWeight = PowerHeuristic(lightPdf, bsdfPdf);
            lightContribution = lightContribution
                + ClampMaxComponentValue(
                    EvaluateBsdf(material, shadingNormal, coatNormal, viewDirection, lightDirection) * lightRadiance
                        * (nDotL * misWeight / lightPdf),
                    kDirectSampleClamp);
        }

        radiance = radiance + lightContribution / static_cast<float>(domeLightSamples);
    }

    return radiance;
}

Ray GenerateCameraRay(const Camera& camera, const RenderSettings& settings, uint32_t x, uint32_t y, const Vec2f& sample) {
    const float filmX = ((static_cast<float>(x) + sample.x) / static_cast<float>(settings.width)) * 2.0f - 1.0f;
    const float filmY = ((static_cast<float>(y) + sample.y) / static_cast<float>(settings.height)) * 2.0f - 1.0f;
    const float tanHalfFov = std::tan(camera.verticalFovDegrees * 0.5f * kPi / 180.0f);

    Vec3f direction = camera.forward
        + camera.right * (filmX * camera.aspectRatio * tanHalfFov)
        + camera.up * (filmY * tanHalfFov);

    return {
        camera.position,
        Normalize(direction),
        kRayEpsilon,
        FLT_MAX,
    };
}

SampleResult TracePath(
    const Scene& scene,
    const AccelerationScene* acceleration,
    const RenderSettings& settings,
    const Ray& primaryRay,
    OpenQmcSampler* sampler) {
    SampleResult result;
    Ray ray = primaryRay;
    Vec3f throughput{1.0f, 1.0f, 1.0f};
    uint32_t diffuseDepth = 0;
    uint32_t specularDepth = 0;
    bool lastBounceSpecular = true;
    bool insideMedium = false;
    bool mediumActive = false;
    Vec3f mediumExtinction{};

    for (uint32_t depth = 0; depth < settings.maxDepth; ++depth) {
        Hit hit;
        if (!IntersectScene(scene, acceleration, ray, &hit)) {
            if (scene.domeLights.empty() || depth == 0 || lastBounceSpecular) {
                if (depth > 0 || settings.backgroundVisible) {
                    result.radiance = result.radiance + throughput * EnvironmentRadiance(scene, ray.direction);
                }
            }
            break;
        }

        if (mediumActive) {
            throughput = throughput * MediumAttenuation(mediumExtinction, hit.distance);
        }

        const PbrMaterial material =
            hit.materialIndex < scene.materials.size() ? scene.materials[hit.materialIndex] : PbrMaterial{};
        const bool frontFace = Dot(hit.geometricNormal, ray.direction) < 0.0f;
        const Vec3f geometricNormal = frontFace ? hit.geometricNormal : hit.geometricNormal * -1.0f;
        Vec3f shadingBaseNormal = frontFace ? hit.shadingNormal : hit.shadingNormal * -1.0f;
        if (Dot(shadingBaseNormal, geometricNormal) < 0.0f) {
            shadingBaseNormal = shadingBaseNormal * -1.0f;
        }
        Vec3f shadingNormal = ResolveShadingNormal(material, shadingBaseNormal, false);
        if (Dot(shadingNormal, geometricNormal) < 0.0f) {
            shadingNormal = shadingNormal * -1.0f;
        }
        const Vec3f coatNormal = ResolveShadingNormal(material, shadingNormal, true);
        const Vec3f viewDirection = ray.direction * -1.0f;
        const float opacity = Clamp(material.opacity, 0.0f, 1.0f);
        if (opacity < 0.999f) {
            if (sampler->Next1D(depth * 8u + 0u) > opacity) {
                ray.origin = hit.position + ray.direction * kRayEpsilon;
                continue;
            }
            throughput = throughput / std::max(opacity, 1.0e-4f);
        }

        if (depth == 0) {
            result.albedo = DiffuseColor(material);
            result.normal = shadingNormal * 0.5f + Vec3f{0.5f, 0.5f, 0.5f};
            result.depth = hit.distance;
        }

        if (material.emissionStrength > 0.0f && (depth == 0 || lastBounceSpecular)) {
            result.radiance = result.radiance + throughput * material.emissionColor * material.emissionStrength;
        }

        result.radiance = result.radiance
            + throughput
                * DirectLighting(
                    scene,
                    acceleration,
                    settings,
                    hit,
                    shadingNormal,
                    coatNormal,
                    material,
                    viewDirection,
                    sampler,
                    depth);

        const float diffuseWeight = DiffuseSampleWeight(material);
        const float baseSpecularWeight = BaseSpecularSampleWeight(material);
        const float coatWeight = CoatSampleWeight(material);
        const float transmissionWeight = TransmissionSampleWeight(material);
        const float reflectiveWeight = diffuseWeight + baseSpecularWeight + coatWeight;
        const float totalWeight = reflectiveWeight + transmissionWeight;
        if (totalWeight <= 0.0f) {
            break;
        }

        const float lobeSelector = sampler->Next1D(depth * 8u + 1u) * totalWeight;
        const Vec2f directionSample = sampler->Next2D(depth * 8u + 2u);

        Vec3f nextDirection{};
        Vec3f bsdfValue{};
        float pdf = 1.0f;
        bool chooseTransmission = false;
        bool chooseBaseSpecular = false;
        bool chooseCoat = false;

        if (lobeSelector < transmissionWeight) {
            chooseTransmission = true;
        } else {
            const float reflectiveSelector = lobeSelector - transmissionWeight;
            if (reflectiveSelector < diffuseWeight) {
            } else if (reflectiveSelector < diffuseWeight + baseSpecularWeight) {
                chooseBaseSpecular = true;
            } else {
                chooseCoat = true;
            }
        }

        if (chooseTransmission) {
            if (specularDepth >= settings.specularDepth) {
                break;
            }
            ++specularDepth;

            const float transmissionProbability = std::max(transmissionWeight / totalWeight, 1.0e-6f);
            const float transmissionRoughness = material.thinWalled
                ? 0.0f
                : AdjustedRoughness(
                    TransmissionRoughness(material),
                    material.specularAnisotropy,
                    material.specularRotation);
            if (material.thinWalled) {
                nextDirection = ray.direction;
                const Vec3f transmissionScale = TransmissionTint(material)
                    * CoatTransmissionScale(material, coatNormal, viewDirection, nextDirection)
                    * (Clamp(material.transmission, 0.0f, 1.0f) * (1.0f - Clamp(material.metallic, 0.0f, 1.0f)));
                const float absCosTheta = std::max(std::fabs(Dot(geometricNormal, nextDirection)), 1.0e-4f);
                bsdfValue = transmissionScale / absCosTheta;
                pdf = transmissionProbability;
            } else {
                const Vec3f microNormal = GgxSampleHalfVector(geometricNormal, directionSample, transmissionRoughness);
                const float etaI = insideMedium ? std::max(material.ior, 1.0f) : 1.0f;
                const float etaT = insideMedium ? 1.0f : std::max(material.ior, 1.0f);
                const float etaRatio = etaI / etaT;
                const float eta = etaT / etaI;
                if (!RefractDirection(ray.direction, microNormal, etaRatio, &nextDirection)) {
                    nextDirection = Normalize(Reflect(ray.direction, microNormal));
                    if (reflectiveWeight <= 0.0f) {
                        break;
                    }
                    bsdfValue = EvaluateBsdf(material, shadingNormal, coatNormal, viewDirection, nextDirection);
                    pdf = std::max((reflectiveWeight / totalWeight)
                            * EvaluateBsdfPdf(material, shadingNormal, coatNormal, viewDirection, nextDirection),
                        1.0e-6f);
                    chooseTransmission = false;
                    chooseBaseSpecular = true;
                } else {
                    bsdfValue = EvaluateTransmissionLobe(
                        material,
                        geometricNormal,
                        coatNormal,
                        viewDirection,
                        nextDirection,
                        eta,
                        transmissionRoughness);
                    pdf = std::max(
                        transmissionProbability
                            * EvaluateTransmissionPdf(
                                geometricNormal,
                                viewDirection,
                                nextDirection,
                                eta,
                                transmissionRoughness),
                        1.0e-6f);
                    insideMedium = !insideMedium;
                    if (insideMedium) {
                        mediumExtinction = MediumExtinction(material);
                        mediumActive = material.transmissionDepth > 0.0f || MaxComponent(material.transmissionScatter) > 0.0f;
                    } else {
                        mediumActive = false;
                    }
                }
            }
        } else if (chooseBaseSpecular || chooseCoat) {
            if (specularDepth >= settings.specularDepth) {
                break;
            }
            ++specularDepth;
            const Vec3f sampleNormal = chooseCoat ? coatNormal : shadingNormal;
            const float sampleRoughness = chooseCoat
                ? AdjustedRoughness(material.coatRoughness, material.coatAnisotropy, material.coatRotation)
                : AdjustedRoughness(BaseLayerRoughness(material), material.specularAnisotropy, material.specularRotation);
            const Vec3f halfVector = GgxSampleHalfVector(sampleNormal, directionSample, sampleRoughness);
            nextDirection = Normalize(Reflect(ray.direction, halfVector));
            const float reflectionCosTheta = Clamp(Dot(sampleNormal, nextDirection), 0.0f, 1.0f);
            if (reflectionCosTheta <= 0.0f) {
                break;
            }
            pdf = std::max(
                (reflectiveWeight / totalWeight)
                    * EvaluateBsdfPdf(material, shadingNormal, coatNormal, viewDirection, nextDirection),
                1.0e-6f);
            bsdfValue = EvaluateBsdf(material, shadingNormal, coatNormal, viewDirection, nextDirection);
        } else {
            if (diffuseDepth >= settings.diffuseDepth) {
                break;
            }
            ++diffuseDepth;
            nextDirection = CosineSampleHemisphere(shadingNormal, directionSample);
            pdf = std::max(
                (reflectiveWeight / totalWeight)
                    * EvaluateBsdfPdf(material, shadingNormal, coatNormal, viewDirection, nextDirection),
                1.0e-6f);
            bsdfValue = EvaluateBsdf(material, shadingNormal, coatNormal, viewDirection, nextDirection);
        }

        float cosTheta = chooseTransmission
            ? std::max(std::fabs(Dot(geometricNormal, nextDirection)), 1.0e-4f)
            : Clamp(Dot(shadingNormal, nextDirection), 0.0f, 1.0f);
        if (cosTheta <= 0.0f) {
            break;
        }

        throughput = ClampMaxComponentValue(throughput * bsdfValue * (cosTheta / pdf), kThroughputClamp);
        const Vec3f rayNormal = chooseTransmission ? geometricNormal : shadingNormal;
        const float originOffset = Dot(rayNormal, nextDirection) >= 0.0f ? 1.0f : -1.0f;
        ray.origin = hit.position + rayNormal * (kRayEpsilon * originOffset);
        ray.direction = nextDirection;
        lastBounceSpecular = chooseBaseSpecular || chooseCoat || chooseTransmission;

        if (depth >= 2) {
            const float continueProbability = Clamp(MaxComponent(throughput), 0.05f, 0.95f);
            if (sampler->Next1D(depth * 8u + 3u) > continueProbability) {
                break;
            }

            throughput = throughput / continueProbability;
        }
    }

    return result;
}

}  // namespace

BackendStatus CpuPathTracer::GetStatus() const {
    BackendStatus status;
    status.kind = BackendKind::Cpu;
    status.name = "cpu";
    status.available = true;
    status.usesGpu = false;
    return status;
}

std::shared_ptr<const AccelerationScene> CpuPathTracer::GetAccelerationScene(
    const Scene& scene,
    const RenderSettings& settings) const {
    std::scoped_lock lock(accelerationMutex_);
    if (cachedAccelerationSource_ == &scene && cachedAcceleration_) {
        return cachedAcceleration_;
    }

#if SHIRO_HAVE_EMBREE
    cachedAcceleration_ = BuildAccelerationScene(scene, settings);
#else
    cachedAcceleration_.reset();
#endif
    cachedAccelerationSource_ = &scene;
    return cachedAcceleration_;
}

FrameBuffer CpuPathTracer::RenderSampleBatch(const RenderRequest& request) const {
    FrameBuffer frame;
    frame.Resize(request.settings.width, request.settings.height);
    frame.Clear();

    if (request.sampleCount == 0) {
        return frame;
    }

    const std::shared_ptr<const AccelerationScene> acceleration =
        GetAccelerationScene(request.scene, request.settings);

    const uint32_t hardwareThreads = std::max(1u, std::thread::hardware_concurrency());
    const uint32_t threadCount = request.settings.threadLimit > 0
        ? std::max(1u, std::min(request.settings.threadLimit, hardwareThreads))
        : (hardwareThreads > 1 ? hardwareThreads - 1 : 1);
    const uint32_t rowsPerThread = std::max(1u, request.settings.height / threadCount);
    std::vector<std::thread> workers;
    workers.reserve(threadCount);

    auto renderRows = [&](uint32_t beginY, uint32_t endY) {
        if (IsCancelled(request.cancel)) {
            return;
        }

        for (uint32_t y = beginY; y < endY; ++y) {
            if (IsCancelled(request.cancel)) {
                return;
            }

            for (uint32_t x = 0; x < request.settings.width; ++x) {
                if (IsCancelled(request.cancel)) {
                    return;
                }

                Vec4f beauty{};
                Vec3f albedo{};
                Vec3f normal{};
                float depth = std::numeric_limits<float>::infinity();

                for (uint32_t sampleIndex = 0; sampleIndex < request.sampleCount; ++sampleIndex) {
                    if (IsCancelled(request.cancel)) {
                        return;
                    }

                    OpenQmcSampler sampler({x, y, request.sampleStart + sampleIndex});
                    const Ray ray = GenerateCameraRay(
                        request.camera,
                        request.settings,
                        x,
                        y,
                        sampler.Next2D(0));
                    const SampleResult sample = TracePath(
                        request.scene,
                        acceleration.get(),
                        request.settings,
                        ray,
                        &sampler);

                    beauty = beauty + Vec4f{
                        sample.radiance.x,
                        sample.radiance.y,
                        sample.radiance.z,
                        sample.depth < std::numeric_limits<float>::infinity() ? 1.0f : 0.0f,
                    };
                    albedo = albedo + sample.albedo;
                    normal = normal + sample.normal;

                    if (sample.depth < depth) {
                        depth = sample.depth;
                    }
                }

                const float inverseSampleCount = 1.0f / static_cast<float>(request.sampleCount);
                frame.SetBeauty(x, y, beauty / static_cast<float>(request.sampleCount));
                frame.SetAlbedo(x, y, albedo * inverseSampleCount);
                frame.SetNormal(x, y, normal * inverseSampleCount);
                frame.SetDepth(x, y, depth);
            }
        }
    };

    for (uint32_t workerIndex = 0; workerIndex < threadCount; ++workerIndex) {
        const uint32_t beginY = workerIndex * rowsPerThread;
        if (beginY >= request.settings.height) {
            break;
        }

        const uint32_t endY = workerIndex + 1 == threadCount
            ? request.settings.height
            : std::min(request.settings.height, beginY + rowsPerThread);
        workers.emplace_back(renderRows, beginY, endY);
    }

    for (std::thread& worker : workers) {
        worker.join();
    }

    return frame;
}

}  // namespace shiro::backend::cpu
