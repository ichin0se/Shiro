#include "shiro/render/Renderer.h"

#include "shiro/render/OpenQmcSampler.h"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <limits>
#include <thread>
#include <vector>

namespace shiro::render {

namespace {

constexpr float kPi = 3.14159265358979323846f;
constexpr float kRayEpsilon = 1.0e-4f;

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
    Vec3f normal;
    uint32_t materialIndex = 0;
};

struct SampleResult {
    Vec3f radiance{};
    Vec3f albedo{};
    Vec3f normal{};
    float depth = std::numeric_limits<float>::infinity();
};

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

Vec3f EnvironmentRadiance(const Scene& scene, const Vec3f& direction) {
    const float t = Clamp(direction.y * 0.5f + 0.5f, 0.0f, 1.0f);
    return Lerp(scene.environmentBottom, scene.environmentTop, t);
}

bool IntersectTriangle(
    const Ray& ray,
    const Vec3f& p0,
    const Vec3f& p1,
    const Vec3f& p2,
    float* outDistance) {
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
    return true;
}

bool IntersectScene(const Scene& scene, const Ray& ray, Hit* outHit) {
    Hit bestHit;

    for (const TriangleMesh& mesh : scene.meshes) {
        for (size_t index = 0; index + 2 < mesh.indices.size(); index += 3) {
            const Vec3f& p0 = mesh.positions[mesh.indices[index + 0]];
            const Vec3f& p1 = mesh.positions[mesh.indices[index + 1]];
            const Vec3f& p2 = mesh.positions[mesh.indices[index + 2]];

            float distance = FLT_MAX;
            if (!IntersectTriangle(ray, p0, p1, p2, &distance) || distance >= bestHit.distance) {
                continue;
            }

            bestHit.hasHit = true;
            bestHit.distance = distance;
            bestHit.position = ray.origin + ray.direction * distance;
            bestHit.normal = Normalize(Cross(p1 - p0, p2 - p0));
            bestHit.materialIndex = mesh.materialIndex;
        }
    }

    if (!bestHit.hasHit) {
        return false;
    }

    *outHit = bestHit;
    return true;
}

bool Occluded(const Scene& scene, const Vec3f& origin, const Vec3f& direction) {
    Hit occlusionHit;
    Ray ray;
    ray.origin = origin;
    ray.direction = direction;
    return IntersectScene(scene, ray, &occlusionHit);
}

Vec3f DirectLighting(const Scene& scene, const Hit& hit, const PbrMaterial& material) {
    Vec3f radiance{};
    const Vec3f n = hit.normal;
    const Vec3f diffuse = material.baseColor / kPi;

    for (const DirectionalLight& light : scene.distantLights) {
        const Vec3f lightDirection = Normalize(-1.0f * light.direction);
        const float nDotL = Clamp(Dot(n, lightDirection), 0.0f, 1.0f);
        if (nDotL <= 0.0f) {
            continue;
        }

        if (Occluded(scene, hit.position + n * kRayEpsilon, lightDirection)) {
            continue;
        }

        radiance = radiance + diffuse * light.radiance * nDotL;
    }

    return radiance;
}

Ray GenerateCameraRay(const Camera& camera, const RenderSettings& settings, uint32_t x, uint32_t y, const Vec2f& sample) {
    const float filmX = ((static_cast<float>(x) + sample.x) / static_cast<float>(settings.width)) * 2.0f - 1.0f;
    const float filmY = ((static_cast<float>(y) + sample.y) / static_cast<float>(settings.height)) * 2.0f - 1.0f;
    const float tanHalfFov = std::tan(camera.verticalFovDegrees * 0.5f * kPi / 180.0f);

    Vec3f direction = camera.forward
        + camera.right * (filmX * camera.aspectRatio * tanHalfFov)
        + camera.up * (-filmY * tanHalfFov);

    return {
        camera.position,
        Normalize(direction),
        kRayEpsilon,
        FLT_MAX,
    };
}

SampleResult TracePath(
    const Scene& scene,
    const RenderSettings& settings,
    const Ray& primaryRay,
    OpenQmcSampler* sampler) {
    SampleResult result;
    Ray ray = primaryRay;
    Vec3f throughput{1.0f, 1.0f, 1.0f};

    for (uint32_t depth = 0; depth < settings.maxDepth; ++depth) {
        Hit hit;
        if (!IntersectScene(scene, ray, &hit)) {
            result.radiance = result.radiance + throughput * EnvironmentRadiance(scene, ray.direction);
            break;
        }

        const PbrMaterial material =
            hit.materialIndex < scene.materials.size() ? scene.materials[hit.materialIndex] : PbrMaterial{};

        if (depth == 0) {
            result.albedo = material.baseColor;
            result.normal = hit.normal * 0.5f + Vec3f{0.5f, 0.5f, 0.5f};
            result.depth = hit.distance;
        }

        result.radiance = result.radiance + throughput * material.emissionColor * material.emissionStrength;
        result.radiance = result.radiance + throughput * DirectLighting(scene, hit, material);

        const Vec2f lobeSample = sampler->Next2D(depth * 2u + 1u);
        const Vec2f directionSample = sampler->Next2D(depth * 2u + 2u);
        const bool chooseSpecular = lobeSample.x < Clamp(material.metallic + (1.0f - material.roughness) * 0.25f, 0.05f, 0.95f);

        Vec3f nextDirection{};
        Vec3f bsdfWeight{};
        float pdf = 1.0f;

        if (chooseSpecular) {
            nextDirection = Reflect(ray.direction, hit.normal);
            nextDirection = Normalize(Lerp(nextDirection, CosineSampleHemisphere(hit.normal, directionSample), material.roughness));
            const Vec3f specularColor = Lerp(Vec3f{0.04f, 0.04f, 0.04f}, material.baseColor, material.metallic);
            const float safeCosTheta = std::max(Clamp(Dot(hit.normal, nextDirection), 0.0f, 1.0f), 1.0e-4f);
            bsdfWeight = specularColor / safeCosTheta;
        } else {
            nextDirection = CosineSampleHemisphere(hit.normal, directionSample);
            const float nDotL = Clamp(Dot(hit.normal, nextDirection), 0.0f, 1.0f);
            pdf = std::max(nDotL / kPi, 1.0e-6f);
            bsdfWeight = material.baseColor / kPi;
        }

        const float cosTheta = Clamp(Dot(hit.normal, nextDirection), 0.0f, 1.0f);
        if (cosTheta <= 0.0f) {
            break;
        }

        throughput = throughput * bsdfWeight * (cosTheta / pdf);
        ray.origin = hit.position + hit.normal * kRayEpsilon;
        ray.direction = nextDirection;

        if (depth >= 2) {
            const float continueProbability = Clamp(MaxComponent(throughput), 0.05f, 0.95f);
            if (sampler->Next1D(depth * 2u + 17u) > continueProbability) {
                break;
            }

            throughput = throughput / continueProbability;
        }
    }

    return result;
}

}  // namespace

void Renderer::SetSettings(const RenderSettings& settings) {
    settings_ = settings;
}

FrameBuffer Renderer::RenderFrame(const Scene& scene, const Camera& camera) const {
    FrameBuffer frame;
    frame.Resize(settings_.width, settings_.height);
    frame.Clear();

    const uint32_t threadCount = std::max(1u, std::thread::hardware_concurrency());
    const uint32_t rowsPerThread = std::max(1u, settings_.height / threadCount);
    std::vector<std::thread> workers;
    workers.reserve(threadCount);

    auto renderRows = [&](uint32_t beginY, uint32_t endY) {
        for (uint32_t y = beginY; y < endY; ++y) {
            for (uint32_t x = 0; x < settings_.width; ++x) {
                Vec4f beauty{};
                Vec3f albedo{};
                Vec3f normal{};
                float depth = std::numeric_limits<float>::infinity();

                for (uint32_t sampleIndex = 0; sampleIndex < settings_.samplesPerPixel; ++sampleIndex) {
                    OpenQmcSampler sampler({x, y, sampleIndex});
                    const Ray ray = GenerateCameraRay(camera, settings_, x, y, sampler.Next2D(0));
                    const SampleResult sample = TracePath(scene, settings_, ray, &sampler);

                    beauty = beauty + Vec4f{
                        sample.radiance.x,
                        sample.radiance.y,
                        sample.radiance.z,
                        sample.depth < std::numeric_limits<float>::infinity() ? 1.0f : 0.0f,
                    };

                    if (sampleIndex == 0) {
                        albedo = sample.albedo;
                        normal = sample.normal;
                        depth = sample.depth;
                    }
                }

                const float divisor = static_cast<float>(std::max(1u, settings_.samplesPerPixel));
                frame.SetBeauty(x, y, beauty / divisor);
                frame.SetAlbedo(x, y, albedo);
                frame.SetNormal(x, y, normal);
                frame.SetDepth(x, y, depth);
            }
        }
    };

    for (uint32_t workerIndex = 0; workerIndex < threadCount; ++workerIndex) {
        const uint32_t beginY = workerIndex * rowsPerThread;
        if (beginY >= settings_.height) {
            break;
        }

        const uint32_t endY = workerIndex + 1 == threadCount
            ? settings_.height
            : std::min(settings_.height, beginY + rowsPerThread);
        workers.emplace_back(renderRows, beginY, endY);
    }

    for (std::thread& worker : workers) {
        worker.join();
    }

    return frame;
}

}  // namespace shiro::render
