#include <optix.h>
#include <optix_device.h>

#include "shiro/backend/optix/OptixLaunchParams.h"

extern "C" {
__constant__ shiro::backend::optix::LaunchParams params;
}

namespace {

using shiro::backend::optix::HitGroupData;
using shiro::backend::optix::OptixDirectionalLight;
using shiro::backend::optix::OptixDomeLight;
using shiro::backend::optix::OptixEnvironmentMapData;
using shiro::backend::optix::OptixUInt3;
using shiro::backend::optix::OptixUInt32;
using shiro::backend::optix::OptixUInt64;
using shiro::backend::optix::OptixVec3f;
using shiro::backend::optix::OptixVec4f;
using uint32_t = OptixUInt32;
using uint64_t = OptixUInt64;

constexpr float kPi = 3.14159265358979323846f;
constexpr float kInvPi = 1.0f / kPi;
constexpr float kInvFourPi = 1.0f / (4.0f * kPi);
constexpr float kRayEpsilon = 1.0e-4f;
constexpr float kInfinity = 1.0e16f;
constexpr float kDirectSampleClamp = 24.0f;
constexpr float kThroughputClamp = 12.0f;
constexpr uint32_t kEnvironmentLayoutLatLong = 1u;
constexpr uint32_t kEnvironmentLayoutAngular = 2u;
constexpr uint32_t kPrimeTable[] = {
    2u, 3u, 5u, 7u, 11u, 13u, 17u, 19u,
    23u, 29u, 31u, 37u, 41u, 43u, 47u, 53u,
    59u, 61u, 67u, 71u, 73u, 79u, 83u, 89u,
};
constexpr float kPrimaryR2Step[] = {
    0.7548776662466927f,
    0.5698402909980532f,
};

struct Ray {
    OptixVec3f origin;
    OptixVec3f direction;
};

struct SampleStream {
    uint32_t pixelIndex;
    uint32_t sampleIndex;
    uint32_t dimension;
    uint32_t scramble;
};

struct SampleResult {
    OptixVec3f radiance;
    OptixVec3f albedo;
    OptixVec3f normal;
    float depth;
};

struct SurfacePayload {
    unsigned int hit;
    float distance;
    OptixVec3f geometricNormal;
    OptixVec3f normal;
    OptixVec3f baseColor;
    OptixVec3f specularColor;
    OptixVec3f transmissionColor;
    OptixVec3f transmissionScatter;
    OptixVec3f coatColor;
    OptixVec3f subsurfaceColor;
    OptixVec3f subsurfaceRadius;
    OptixVec3f sheenColor;
    OptixVec3f emissionColor;
    OptixVec3f normalOverride;
    OptixVec3f coatNormalOverride;
    OptixVec3f tangentOverride;
    float baseWeight;
    float emissionStrength;
    float specularWeight;
    float metallic;
    float roughness;
    float diffuseRoughness;
    float opacity;
    float specularAnisotropy;
    float specularRotation;
    float coatWeight;
    float coatRoughness;
    float coatIor;
    float coatAnisotropy;
    float coatRotation;
    float sheen;
    float sheenRoughness;
    float subsurface;
    float subsurfaceScale;
    float subsurfaceAnisotropy;
    float transmission;
    float transmissionDepth;
    float transmissionScatterAnisotropy;
    float transmissionDispersion;
    float transmissionExtraRoughness;
    float ior;
    float coatAffectColor;
    float coatAffectRoughness;
    float thinFilmThickness;
    float thinFilmIor;
    unsigned int thinWalled;
    unsigned int hasNormalOverride;
    unsigned int hasCoatNormalOverride;
    unsigned int hasTangentOverride;
};

static __forceinline__ __device__ OptixVec3f MakeVec3f(float x, float y, float z) {
    return {x, y, z};
}

static __forceinline__ __device__ OptixVec3f Add(const OptixVec3f& lhs, const OptixVec3f& rhs) {
    return {lhs.x + rhs.x, lhs.y + rhs.y, lhs.z + rhs.z};
}

static __forceinline__ __device__ OptixVec3f Sub(const OptixVec3f& lhs, const OptixVec3f& rhs) {
    return {lhs.x - rhs.x, lhs.y - rhs.y, lhs.z - rhs.z};
}

static __forceinline__ __device__ OptixVec3f Mul(const OptixVec3f& lhs, float rhs) {
    return {lhs.x * rhs, lhs.y * rhs, lhs.z * rhs};
}

static __forceinline__ __device__ OptixVec3f Mul(const OptixVec3f& lhs, const OptixVec3f& rhs) {
    return {lhs.x * rhs.x, lhs.y * rhs.y, lhs.z * rhs.z};
}

static __forceinline__ __device__ OptixVec3f Div(const OptixVec3f& lhs, float rhs) {
    return {lhs.x / rhs, lhs.y / rhs, lhs.z / rhs};
}

static __forceinline__ __device__ OptixVec3f operator+(const OptixVec3f& lhs, const OptixVec3f& rhs) {
    return Add(lhs, rhs);
}

static __forceinline__ __device__ OptixVec3f operator-(const OptixVec3f& lhs, const OptixVec3f& rhs) {
    return Sub(lhs, rhs);
}

static __forceinline__ __device__ OptixVec3f operator*(const OptixVec3f& lhs, float rhs) {
    return Mul(lhs, rhs);
}

static __forceinline__ __device__ OptixVec3f operator*(float lhs, const OptixVec3f& rhs) {
    return Mul(rhs, lhs);
}

static __forceinline__ __device__ OptixVec3f operator*(const OptixVec3f& lhs, const OptixVec3f& rhs) {
    return Mul(lhs, rhs);
}

static __forceinline__ __device__ OptixVec3f operator/(const OptixVec3f& lhs, float rhs) {
    return Div(lhs, rhs);
}

static __forceinline__ __device__ float Dot(const OptixVec3f& lhs, const OptixVec3f& rhs) {
    return lhs.x * rhs.x + lhs.y * rhs.y + lhs.z * rhs.z;
}

static __forceinline__ __device__ OptixVec3f Cross(const OptixVec3f& lhs, const OptixVec3f& rhs) {
    return {
        lhs.y * rhs.z - lhs.z * rhs.y,
        lhs.z * rhs.x - lhs.x * rhs.z,
        lhs.x * rhs.y - lhs.y * rhs.x,
    };
}

static __forceinline__ __device__ float Length(const OptixVec3f& value) {
    return sqrtf(Dot(value, value));
}

static __forceinline__ __device__ OptixVec3f Normalize(const OptixVec3f& value) {
    const float length = Length(value);
    return length > 0.0f ? Div(value, length) : MakeVec3f(0.0f, 0.0f, 0.0f);
}

static __forceinline__ __device__ OptixVec3f SafeNormalized(const OptixVec3f& value, const OptixVec3f& fallback) {
    const float length = Length(value);
    return length > 0.0f ? Div(value, length) : fallback;
}

static __forceinline__ __device__ OptixVec3f Exp(const OptixVec3f& value) {
    return MakeVec3f(expf(value.x), expf(value.y), expf(value.z));
}

static __forceinline__ __device__ float Clamp(float value, float lo, float hi) {
    return fminf(fmaxf(value, lo), hi);
}

static __forceinline__ __device__ float Square(float value) {
    return value * value;
}

static __forceinline__ __device__ OptixVec3f BuildOrthonormalX(const OptixVec3f& normal);

static __forceinline__ __device__ float AdjustedRoughness(float roughness, float anisotropy, float rotation) {
    const float clampedAnisotropy = Clamp(anisotropy, -0.95f, 0.95f);
    const float rotationPhase = cosf(rotation * 2.0f * kPi);
    const float adjusted = roughness * (1.0f - 0.35f * clampedAnisotropy * rotationPhase);
    return Clamp(adjusted, 0.02f, 1.0f);
}

static __forceinline__ __device__ OptixVec3f ProjectedTangent(
    const SurfacePayload& material,
    const OptixVec3f& normal) {
    if (material.hasTangentOverride != 0u) {
        const OptixVec3f tangent = material.tangentOverride - normal * Dot(material.tangentOverride, normal);
        if (Length(tangent) > 0.0f) {
            return Normalize(tangent);
        }
    }
    return BuildOrthonormalX(normal);
}

static __forceinline__ __device__ float AnisotropyPhase(
    const OptixVec3f& normal,
    const OptixVec3f& tangent,
    const OptixVec3f& viewDirection,
    const OptixVec3f& lightDirection,
    float rotation) {
    const OptixVec3f tangentDirection =
        SafeNormalized(tangent - normal * Dot(tangent, normal), BuildOrthonormalX(normal));
    const OptixVec3f bitangent = Normalize(Cross(normal, tangentDirection));
    const OptixVec3f halfVector = SafeNormalized(viewDirection + lightDirection, normal);
    const OptixVec3f halfProjected = halfVector - normal * Dot(normal, halfVector);
    if (Length(halfProjected) <= 1.0e-6f) {
        return cosf(rotation * 2.0f * kPi);
    }

    const OptixVec3f halfDirection = Normalize(halfProjected);
    const float angle = atan2f(Dot(halfDirection, bitangent), Dot(halfDirection, tangentDirection));
    return cosf(angle - rotation * 2.0f * kPi);
}

static __forceinline__ __device__ float AdjustedRoughness(
    float roughness,
    float anisotropy,
    float rotation,
    const OptixVec3f& normal,
    const OptixVec3f& tangent,
    const OptixVec3f& viewDirection,
    const OptixVec3f& lightDirection) {
    const float clampedAnisotropy = Clamp(anisotropy, -0.95f, 0.95f);
    const float orientationPhase = AnisotropyPhase(normal, tangent, viewDirection, lightDirection, rotation);
    const float adjusted = roughness * (1.0f - 0.35f * clampedAnisotropy * orientationPhase);
    return Clamp(adjusted, 0.02f, 1.0f);
}

static __forceinline__ __device__ OptixVec3f Lerp(const OptixVec3f& lhs, const OptixVec3f& rhs, float t) {
    return Add(Mul(lhs, 1.0f - t), Mul(rhs, t));
}

static __forceinline__ __device__ float MaxComponent(const OptixVec3f& value) {
    return fmaxf(value.x, fmaxf(value.y, value.z));
}

static __forceinline__ __device__ OptixVec3f Reflect(const OptixVec3f& incident, const OptixVec3f& normal) {
    return Sub(incident, Mul(normal, 2.0f * Dot(incident, normal)));
}

static __forceinline__ __device__ void PackPointer(const void* ptr, unsigned int& low, unsigned int& high) {
    const unsigned long long value = reinterpret_cast<unsigned long long>(ptr);
    low = static_cast<unsigned int>(value & 0xffffffffull);
    high = static_cast<unsigned int>(value >> 32);
}

template <typename T>
static __forceinline__ __device__ T* UnpackPointer(unsigned int low, unsigned int high) {
    const unsigned long long value =
        static_cast<unsigned long long>(low) | (static_cast<unsigned long long>(high) << 32);
    return reinterpret_cast<T*>(value);
}

template <typename T>
static __forceinline__ __device__ T* GetPayload() {
    return UnpackPointer<T>(optixGetPayload_0(), optixGetPayload_1());
}

static __forceinline__ __device__ unsigned int MixBits(unsigned int state) {
    state ^= state >> 16;
    state *= 0x7feb352du;
    state ^= state >> 15;
    state *= 0x846ca68bu;
    state ^= state >> 16;
    return state;
}

static __forceinline__ __device__ float HashToUnitFloat(uint32_t bits) {
    return static_cast<float>(MixBits(bits) & 0x00ffffffu) / 16777216.0f;
}

static __forceinline__ __device__ float RadicalInverse(uint32_t base, uint32_t index) {
    const float inverseBase = 1.0f / static_cast<float>(base);
    float inverseBi = inverseBase;
    float reversed = 0.0f;
    while (index > 0u) {
        const uint32_t digit = index % base;
        reversed += static_cast<float>(digit) * inverseBi;
        index /= base;
        inverseBi *= inverseBase;
    }
    return reversed;
}

static __forceinline__ __device__ SampleStream MakeSampleStream(uint32_t pixelIndex, uint32_t sampleIndex) {
    SampleStream stream{};
    stream.pixelIndex = pixelIndex;
    stream.sampleIndex = params.sampleStart + sampleIndex;
    stream.dimension = 0u;

    uint32_t scramble = pixelIndex * 0x45d9f3bu;
    scramble ^= (params.sampleStart + sampleIndex + 1u) * 0x27d4eb2du;
    scramble ^= params.width * 0x165667b1u;
    stream.scramble = MixBits(scramble);
    return stream;
}

static __forceinline__ __device__ float RandomFloat(SampleStream& stream) {
    if (stream.dimension < 2u) {
        const float value =
            0.5f + kPrimaryR2Step[stream.dimension] * static_cast<float>(stream.sampleIndex);
        ++stream.dimension;
        return value - floorf(value);
    }

    const uint32_t base =
        kPrimeTable[stream.dimension % (sizeof(kPrimeTable) / sizeof(kPrimeTable[0]))];
    const float sequence = RadicalInverse(base, stream.sampleIndex + 1u);
    const float rotation =
        HashToUnitFloat(stream.scramble ^ ((stream.dimension + 1u) * 0x9e3779b9u));
    ++stream.dimension;
    const float value = sequence + rotation;
    return value - floorf(value);
}

static __forceinline__ __device__ OptixVec3f BuildOrthonormalX(const OptixVec3f& normal) {
    const OptixVec3f axis = fabsf(normal.z) < 0.999f
        ? MakeVec3f(0.0f, 0.0f, 1.0f)
        : MakeVec3f(0.0f, 1.0f, 0.0f);
    return Normalize(Cross(axis, normal));
}

static __forceinline__ __device__ OptixVec3f CosineSampleHemisphere(const OptixVec3f& normal, SampleStream& stream) {
    const float u1 = RandomFloat(stream);
    const float u2 = RandomFloat(stream);
    const float phi = 2.0f * kPi * u1;
    const float radius = sqrtf(u2);
    const float x = radius * cosf(phi);
    const float y = radius * sinf(phi);
    const float z = sqrtf(fmaxf(0.0f, 1.0f - u2));

    const OptixVec3f tangent = BuildOrthonormalX(normal);
    const OptixVec3f bitangent = Cross(normal, tangent);
    return Normalize(Add(Add(Mul(tangent, x), Mul(bitangent, y)), Mul(normal, z)));
}

static __forceinline__ __device__ OptixVec3f GgxSampleHalfVector(
    const OptixVec3f& normal,
    SampleStream& stream,
    float roughness) {
    const float u1 = RandomFloat(stream);
    const float u2 = RandomFloat(stream);
    const float alpha = fmaxf(0.02f, roughness * roughness);
    const float alpha2 = alpha * alpha;
    const float phi = 2.0f * kPi * u1;
    const float cosTheta = sqrtf(
        fmaxf(0.0f, (1.0f - u2) / fmaxf(1.0e-6f, 1.0f + (alpha2 - 1.0f) * u2)));
    const float sinTheta = sqrtf(fmaxf(0.0f, 1.0f - cosTheta * cosTheta));
    const float x = sinTheta * cosf(phi);
    const float y = sinTheta * sinf(phi);
    const float z = cosTheta;

    const OptixVec3f tangent = BuildOrthonormalX(normal);
    const OptixVec3f bitangent = Cross(normal, tangent);
    return Normalize(Add(Add(Mul(tangent, x), Mul(bitangent, y)), Mul(normal, z)));
}

static __forceinline__ __device__ OptixVec3f UniformSampleSphere(SampleStream& stream) {
    const float u1 = RandomFloat(stream);
    const float u2 = RandomFloat(stream);
    const float y = 1.0f - 2.0f * u2;
    const float radius = sqrtf(fmaxf(0.0f, 1.0f - y * y));
    const float phi = 2.0f * kPi * u1;
    return {
        sinf(phi) * radius,
        y,
        cosf(phi) * radius,
    };
}

static __forceinline__ __device__ float WrapUnit(float value) {
    value = fmodf(value, 1.0f);
    return value < 0.0f ? value + 1.0f : value;
}

static __forceinline__ __device__ float DielectricF0Scalar(float ior) {
    const float safeIor = fmaxf(ior, 1.0f);
    const float ratio = (safeIor - 1.0f) / (safeIor + 1.0f);
    return ratio * ratio;
}

static __forceinline__ __device__ OptixVec3f DiffuseColor(const SurfacePayload& material) {
    const float metallic = Clamp(material.metallic, 0.0f, 1.0f);
    const float transmission = Clamp(material.transmission, 0.0f, 1.0f);
    const float subsurface = Clamp(material.subsurface, 0.0f, 1.0f);
    const float radiusBlend = Clamp(MaxComponent(material.subsurfaceRadius) / 3.0f, 0.0f, 1.0f);
    const OptixVec3f subsurfaceTint = Lerp(material.baseColor, material.subsurfaceColor, radiusBlend);
    const OptixVec3f diffuseBase = Lerp(material.baseColor, subsurfaceTint, subsurface);
    return diffuseBase * (Clamp(material.baseWeight, 0.0f, 1.0f) * (1.0f - metallic) * (1.0f - transmission));
}

static __forceinline__ __device__ float BaseLayerRoughness(const SurfacePayload& material) {
    return Clamp(
        material.roughness + material.coatAffectRoughness * material.coatWeight * material.coatRoughness,
        0.0f,
        1.0f);
}

static __forceinline__ __device__ OptixVec3f SpecularF0(const SurfacePayload& material) {
    const float metallic = Clamp(material.metallic, 0.0f, 1.0f);
    const OptixVec3f dielectricF0 =
        material.specularColor
        * (Clamp(material.specularWeight, 0.0f, 1.0f) * DielectricF0Scalar(material.ior));
    return Lerp(dielectricF0, material.baseColor, metallic);
}

static __forceinline__ __device__ OptixVec3f CoatF0(const SurfacePayload& material) {
    return material.coatColor * (Clamp(material.coatWeight, 0.0f, 1.0f) * DielectricF0Scalar(material.coatIor));
}

static __forceinline__ __device__ float TransmissionRoughness(const SurfacePayload& material) {
    return Clamp(material.roughness + material.transmissionExtraRoughness, 0.0f, 1.0f);
}

static __forceinline__ __device__ OptixVec3f ThinFilmColor(float thickness, float ior, float vDotH) {
    if (thickness <= 0.0f) {
        return MakeVec3f(1.0f, 1.0f, 1.0f);
    }

    const float safeIor = fmaxf(ior, 1.0f);
    const float opticalPath = 2.0f * safeIor * thickness * Clamp(vDotH, 0.05f, 1.0f);
    const auto spectralBand = [&](float wavelength) {
        const float phase = 4.0f * kPi * opticalPath / fmaxf(wavelength, 1.0f);
        return 0.5f + 0.5f * cosf(phase);
    };

    const OptixVec3f iridescence = MakeVec3f(
        spectralBand(650.0f),
        spectralBand(510.0f),
        spectralBand(475.0f));
    const float strength = Clamp(thickness / 1200.0f, 0.0f, 1.0f);
    return Lerp(
        MakeVec3f(1.0f, 1.0f, 1.0f),
        (MakeVec3f(1.0f, 1.0f, 1.0f) + iridescence) * 0.5f,
        strength);
}

static __forceinline__ __device__ OptixVec3f ApplyThinFilm(
    const OptixVec3f& lobe,
    const SurfacePayload& material,
    const OptixVec3f& normal,
    const OptixVec3f& viewDirection,
    const OptixVec3f& lightDirection) {
    if (material.thinFilmThickness <= 0.0f) {
        return lobe;
    }

    const OptixVec3f halfVector = SafeNormalized(viewDirection + lightDirection, normal);
    const float vDotH = Clamp(Dot(viewDirection, halfVector), 0.0f, 1.0f);
    return lobe * ThinFilmColor(material.thinFilmThickness, material.thinFilmIor, vDotH);
}

static __forceinline__ __device__ OptixVec3f ResolveShadingNormal(
    const SurfacePayload& material,
    const OptixVec3f& fallback,
    bool coatLayer) {
    const unsigned int hasOverride = coatLayer ? material.hasCoatNormalOverride : material.hasNormalOverride;
    if (hasOverride == 0u) {
        return fallback;
    }

    const OptixVec3f candidate = coatLayer ? material.coatNormalOverride : material.normalOverride;
    if (Length(candidate) <= 0.0f) {
        return fallback;
    }

    OptixVec3f normal = Normalize(candidate);
    if (Dot(normal, fallback) < 0.0f) {
        normal = normal * -1.0f;
    }
    return normal;
}

static __forceinline__ __device__ float GgxDistribution(float nDotH, float roughness) {
    const float alpha = fmaxf(0.02f, roughness * roughness);
    const float alpha2 = alpha * alpha;
    const float denom = fmaxf(1.0e-6f, Square(nDotH * nDotH * (alpha2 - 1.0f) + 1.0f));
    return alpha2 / (kPi * denom);
}

static __forceinline__ __device__ float GgxSmithVisibility(float nDotV, float nDotL, float roughness) {
    const float alpha = fmaxf(0.02f, roughness * roughness);
    const float alpha2 = alpha * alpha;
    const auto lambda = [&](float nDot) {
        const float clamped = fmaxf(nDot, 1.0e-6f);
        return (-1.0f + sqrtf(1.0f + alpha2 * (1.0f - clamped * clamped) / (clamped * clamped))) * 0.5f;
    };
    return 1.0f / (1.0f + lambda(nDotV) + lambda(nDotL));
}

static __forceinline__ __device__ OptixVec3f FresnelSchlick(const OptixVec3f& f0, float vDotH) {
    const float clamped = Clamp(1.0f - vDotH, 0.0f, 1.0f);
    const float factor = clamped * clamped * clamped * clamped * clamped;
    return f0 + (MakeVec3f(1.0f, 1.0f, 1.0f) - f0) * factor;
}

static __forceinline__ __device__ OptixVec3f OneMinusClamped(const OptixVec3f& value) {
    return MakeVec3f(
        Clamp(1.0f - value.x, 0.0f, 1.0f),
        Clamp(1.0f - value.y, 0.0f, 1.0f),
        Clamp(1.0f - value.z, 0.0f, 1.0f));
}

static __forceinline__ __device__ OptixVec3f CoatUnderlayerTint(const SurfacePayload& material) {
    const float coatAffect = Clamp(material.coatAffectColor * material.coatWeight, 0.0f, 1.0f);
    return Lerp(
        MakeVec3f(1.0f, 1.0f, 1.0f),
        MakeVec3f(
            Clamp(material.coatColor.x, 0.0f, 1.0f),
            Clamp(material.coatColor.y, 0.0f, 1.0f),
            Clamp(material.coatColor.z, 0.0f, 1.0f)),
        coatAffect);
}

static __forceinline__ __device__ OptixVec3f DirectionalLayerTransmittance(
    const OptixVec3f& f0,
    float nDotV,
    float nDotL) {
    const OptixVec3f fresnelV = FresnelSchlick(f0, Clamp(nDotV, 0.0f, 1.0f));
    const OptixVec3f fresnelL = FresnelSchlick(f0, Clamp(nDotL, 0.0f, 1.0f));
    return OneMinusClamped((fresnelV + fresnelL) * 0.5f);
}

static __forceinline__ __device__ OptixVec3f ViewLayerTransmittance(
    const OptixVec3f& f0,
    float nDotV) {
    return OneMinusClamped(FresnelSchlick(f0, Clamp(nDotV, 0.0f, 1.0f)));
}

static __forceinline__ __device__ OptixVec3f TransmissionInterfaceTransmittance(
    const OptixVec3f& f0,
    float cosThetaA,
    float cosThetaB) {
    const OptixVec3f fresnelA = FresnelSchlick(f0, Clamp(fabsf(cosThetaA), 0.0f, 1.0f));
    const OptixVec3f fresnelB = FresnelSchlick(f0, Clamp(fabsf(cosThetaB), 0.0f, 1.0f));
    return OneMinusClamped((fresnelA + fresnelB) * 0.5f);
}

static __forceinline__ __device__ OptixVec3f CoatUnderlayerTransmittance(
    const SurfacePayload& material,
    const OptixVec3f& coatNormal,
    const OptixVec3f& viewDirection,
    const OptixVec3f& lightDirection) {
    return CoatUnderlayerTint(material)
        * DirectionalLayerTransmittance(
            CoatF0(material),
            Dot(coatNormal, viewDirection),
            Dot(coatNormal, lightDirection));
}

static __forceinline__ __device__ OptixVec3f CoatViewTransmittance(
    const SurfacePayload& material,
    const OptixVec3f& coatNormal,
    const OptixVec3f& viewDirection) {
    return CoatUnderlayerTint(material)
        * ViewLayerTransmittance(CoatF0(material), Dot(coatNormal, viewDirection));
}

static __forceinline__ __device__ OptixVec3f CoatTransmissionScale(
    const SurfacePayload& material,
    const OptixVec3f& coatNormal,
    const OptixVec3f& viewDirection,
    const OptixVec3f& lightDirection) {
    return CoatUnderlayerTint(material)
        * TransmissionInterfaceTransmittance(
            CoatF0(material),
            Dot(coatNormal, viewDirection),
            Dot(coatNormal, lightDirection));
}

static __forceinline__ __device__ OptixVec3f EvaluateSpecularLobe(
    const OptixVec3f& normal,
    const OptixVec3f& viewDirection,
    const OptixVec3f& lightDirection,
    const OptixVec3f& f0,
    float roughness) {
    const float nDotV = Clamp(Dot(normal, viewDirection), 0.0f, 1.0f);
    const float nDotL = Clamp(Dot(normal, lightDirection), 0.0f, 1.0f);
    if (nDotV <= 0.0f || nDotL <= 0.0f) {
        return MakeVec3f(0.0f, 0.0f, 0.0f);
    }

    const OptixVec3f halfVector = SafeNormalized(viewDirection + lightDirection, normal);
    const float nDotH = Clamp(Dot(normal, halfVector), 0.0f, 1.0f);
    const float vDotH = Clamp(Dot(viewDirection, halfVector), 0.0f, 1.0f);
    const float distribution = GgxDistribution(nDotH, roughness);
    const float visibility = GgxSmithVisibility(nDotV, nDotL, roughness);
    const OptixVec3f fresnel = FresnelSchlick(f0, vDotH);
    return fresnel * (distribution * visibility / fmaxf(4.0f * nDotV * nDotL, 1.0e-6f));
}

static __forceinline__ __device__ float EvaluateSpecularPdf(
    const OptixVec3f& normal,
    const OptixVec3f& viewDirection,
    const OptixVec3f& lightDirection,
    float roughness) {
    const float nDotV = Clamp(Dot(normal, viewDirection), 0.0f, 1.0f);
    const float nDotL = Clamp(Dot(normal, lightDirection), 0.0f, 1.0f);
    if (nDotV <= 0.0f || nDotL <= 0.0f) {
        return 0.0f;
    }

    const OptixVec3f halfVector = SafeNormalized(viewDirection + lightDirection, normal);
    const float nDotH = Clamp(Dot(normal, halfVector), 0.0f, 1.0f);
    const float vDotH = Clamp(Dot(viewDirection, halfVector), 1.0e-6f, 1.0f);
    return GgxDistribution(nDotH, roughness) * nDotH / fmaxf(4.0f * vDotH, 1.0e-6f);
}

static __forceinline__ __device__ OptixVec3f TransmissionTint(const SurfacePayload& material);

static __forceinline__ __device__ OptixVec3f EvaluateTransmissionLobe(
    const SurfacePayload& material,
    const OptixVec3f& normal,
    const OptixVec3f& coatNormal,
    const OptixVec3f& viewDirection,
    const OptixVec3f& lightDirection,
    float eta,
    float roughness) {
    const float cosWo = Dot(normal, viewDirection);
    const float cosWi = Dot(normal, lightDirection);
    const float absCosWo = fabsf(cosWo);
    const float absCosWi = fabsf(cosWi);
    if (absCosWo <= 0.0f || absCosWi <= 0.0f || cosWo * cosWi >= 0.0f) {
        return MakeVec3f(0.0f, 0.0f, 0.0f);
    }

    OptixVec3f halfVector = SafeNormalized(viewDirection + lightDirection * eta, normal);
    if (Dot(normal, halfVector) < 0.0f) {
        halfVector = halfVector * -1.0f;
    }

    const float woDotH = Dot(viewDirection, halfVector);
    const float wiDotH = Dot(lightDirection, halfVector);
    if (woDotH * wiDotH >= 0.0f) {
        return MakeVec3f(0.0f, 0.0f, 0.0f);
    }

    const float nDotH = Clamp(Dot(normal, halfVector), 0.0f, 1.0f);
    const float sqrtDenom = woDotH + eta * wiDotH;
    if (nDotH <= 0.0f || fabsf(sqrtDenom) <= 1.0e-6f) {
        return MakeVec3f(0.0f, 0.0f, 0.0f);
    }

    const float distribution = GgxDistribution(nDotH, roughness);
    const float visibility = GgxSmithVisibility(absCosWo, absCosWi, roughness);
    const OptixVec3f fresnel =
        OneMinusClamped(FresnelSchlick(SpecularF0(material), Clamp(fabsf(woDotH), 0.0f, 1.0f)));
    const OptixVec3f transmissionScale = TransmissionTint(material)
        * CoatTransmissionScale(material, coatNormal, viewDirection, lightDirection)
        * (Clamp(material.transmission, 0.0f, 1.0f) * (1.0f - Clamp(material.metallic, 0.0f, 1.0f)));
    const float factor = fabsf(
        distribution * visibility * fabsf(wiDotH) * fabsf(woDotH)
        / fmaxf(absCosWo * absCosWi * Square(sqrtDenom), 1.0e-6f));
    return transmissionScale * fresnel * factor;
}

static __forceinline__ __device__ float EvaluateTransmissionPdf(
    const OptixVec3f& normal,
    const OptixVec3f& viewDirection,
    const OptixVec3f& lightDirection,
    float eta,
    float roughness) {
    const float cosWo = Dot(normal, viewDirection);
    const float cosWi = Dot(normal, lightDirection);
    if (fabsf(cosWo) <= 0.0f || fabsf(cosWi) <= 0.0f || cosWo * cosWi >= 0.0f) {
        return 0.0f;
    }

    OptixVec3f halfVector = SafeNormalized(viewDirection + lightDirection * eta, normal);
    if (Dot(normal, halfVector) < 0.0f) {
        halfVector = halfVector * -1.0f;
    }

    const float woDotH = Dot(viewDirection, halfVector);
    const float wiDotH = Dot(lightDirection, halfVector);
    const float sqrtDenom = woDotH + eta * wiDotH;
    if (woDotH * wiDotH >= 0.0f || fabsf(sqrtDenom) <= 1.0e-6f) {
        return 0.0f;
    }

    const float nDotH = Clamp(Dot(normal, halfVector), 0.0f, 1.0f);
    const float pdfWh = GgxDistribution(nDotH, roughness) * nDotH;
    const float dwhDwi = fabsf((eta * eta * wiDotH) / Square(sqrtDenom));
    return pdfWh * dwhDwi;
}

static __forceinline__ __device__ float DiffuseSampleWeight(const SurfacePayload& material);
static __forceinline__ __device__ float BaseSpecularSampleWeight(const SurfacePayload& material);
static __forceinline__ __device__ float CoatSampleWeight(const SurfacePayload& material);

static __forceinline__ __device__ float PowerHeuristic(float lhsPdf, float rhsPdf) {
    const float lhs2 = lhsPdf * lhsPdf;
    const float rhs2 = rhsPdf * rhsPdf;
    return lhs2 / fmaxf(lhs2 + rhs2, 1.0e-6f);
}

static __forceinline__ __device__ OptixVec3f ClampMaxComponentValue(
    const OptixVec3f& value,
    float maxComponent) {
    const float current = MaxComponent(value);
    if (current <= maxComponent || current <= 0.0f) {
        return value;
    }
    return value * (maxComponent / current);
}

static __forceinline__ __device__ float EvaluateBsdfPdf(
    const SurfacePayload& material,
    const OptixVec3f& normal,
    const OptixVec3f& coatNormal,
    const OptixVec3f& viewDirection,
    const OptixVec3f& lightDirection) {
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

static __forceinline__ __device__ OptixVec3f EvaluateBsdf(
    const SurfacePayload& material,
    const OptixVec3f& normal,
    const OptixVec3f& coatNormal,
    const OptixVec3f& viewDirection,
    const OptixVec3f& lightDirection) {
    const float nDotV = Clamp(Dot(normal, viewDirection), 0.0f, 1.0f);
    const float nDotL = Clamp(Dot(normal, lightDirection), 0.0f, 1.0f);
    if (nDotV <= 0.0f || nDotL <= 0.0f) {
        return MakeVec3f(0.0f, 0.0f, 0.0f);
    }

    const OptixVec3f baseF0 = SpecularF0(material);
    const OptixVec3f coatF0 = CoatF0(material);
    const OptixVec3f underCoat = CoatUnderlayerTransmittance(material, coatNormal, viewDirection, lightDirection);
    const OptixVec3f underBase = DirectionalLayerTransmittance(baseF0, nDotV, nDotL);
    const OptixVec3f tangent = ProjectedTangent(material, normal);
    const OptixVec3f coatTangent = ProjectedTangent(material, coatNormal);
    const OptixVec3f viewProjected = Normalize(Sub(viewDirection, Mul(normal, nDotV)));
    const OptixVec3f lightProjected = Normalize(Sub(lightDirection, Mul(normal, nDotL)));
    const float cosPhi = (Length(viewProjected) > 0.0f && Length(lightProjected) > 0.0f)
        ? Clamp(Dot(viewProjected, lightProjected), -1.0f, 1.0f)
        : 0.0f;
    const float sigma = Clamp(material.diffuseRoughness, 0.0f, 1.0f);
    const float sigma2 = sigma * sigma;
    const float orenA = 1.0f - (sigma2 / (2.0f * (sigma2 + 0.33f)));
    const float orenB = 0.45f * sigma2 / (sigma2 + 0.09f);
    const float sinAlpha = fmaxf(
        sqrtf(fmaxf(0.0f, 1.0f - nDotV * nDotV)),
        sqrtf(fmaxf(0.0f, 1.0f - nDotL * nDotL)));
    const float tanBeta = fminf(
        sqrtf(fmaxf(0.0f, 1.0f - nDotV * nDotV)) / fmaxf(nDotV, 1.0e-6f),
        sqrtf(fmaxf(0.0f, 1.0f - nDotL * nDotL)) / fmaxf(nDotL, 1.0e-6f));
    const float wrap = Clamp(
        material.subsurface
            * material.subsurfaceScale
            * (0.35f + 0.15f * Clamp(material.subsurfaceAnisotropy, -1.0f, 1.0f)
                + 0.1f * Clamp(MaxComponent(material.subsurfaceRadius) / 3.0f, 0.0f, 1.0f)),
        0.0f,
        0.9f);
    const float wrappedNDotL = Clamp((nDotL + wrap) / (1.0f + wrap), 0.0f, 1.0f);
    const float diffuseTerm = (orenA + orenB * fmaxf(0.0f, cosPhi) * sinAlpha * tanBeta)
        * (wrappedNDotL / fmaxf(nDotL, 1.0e-6f));
    const OptixVec3f diffuse = DiffuseColor(material) * underCoat * underBase * (kInvPi * diffuseTerm);
    const float specularRoughness = AdjustedRoughness(
        BaseLayerRoughness(material),
        material.specularAnisotropy,
        material.specularRotation,
        normal,
        tangent,
        viewDirection,
        lightDirection);
    const OptixVec3f specular = underCoat * ApplyThinFilm(
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
    const OptixVec3f coat = ApplyThinFilm(
        EvaluateSpecularLobe(coatNormal, viewDirection, lightDirection, coatF0, coatLobeRoughness),
        material,
        coatNormal,
        viewDirection,
        lightDirection);
    const OptixVec3f halfVector = SafeNormalized(viewDirection + lightDirection, normal);
    const float lDotH = Clamp(Dot(lightDirection, halfVector), 0.0f, 1.0f);
    const float sheenFactor =
        powf(1.0f - lDotH, 5.0f) * (1.0f - 0.5f * Clamp(material.sheenRoughness, 0.0f, 1.0f));
    const OptixVec3f sheen =
        material.sheenColor * Clamp(material.sheen, 0.0f, 1.0f) * sheenFactor * underCoat * underBase;
    return diffuse + specular + coat + sheen;
}

static __forceinline__ __device__ bool RefractDirection(
    const OptixVec3f& incident,
    const OptixVec3f& normal,
    float eta,
    OptixVec3f* refracted) {
    if (!refracted) {
        return false;
    }

    const float cosTheta = Clamp(-Dot(incident, normal), -1.0f, 1.0f);
    const float sin2Theta = fmaxf(0.0f, 1.0f - cosTheta * cosTheta);
    const float k = 1.0f - eta * eta * sin2Theta;
    if (k <= 0.0f) {
        return false;
    }

    *refracted = Normalize(incident * eta + normal * (eta * cosTheta - sqrtf(k)));
    return true;
}

static __forceinline__ __device__ float DiffuseSampleWeight(const SurfacePayload& material) {
    const OptixVec3f underCoat = CoatUnderlayerTint(material) * OneMinusClamped(CoatF0(material));
    const OptixVec3f underBase = OneMinusClamped(SpecularF0(material));
    return fmaxf(0.0f, MaxComponent(DiffuseColor(material) * underCoat * underBase));
}

static __forceinline__ __device__ float BaseSpecularSampleWeight(const SurfacePayload& material) {
    const OptixVec3f underCoat = CoatUnderlayerTint(material) * OneMinusClamped(CoatF0(material));
    return fmaxf(0.0f, MaxComponent(underCoat * SpecularF0(material)));
}

static __forceinline__ __device__ float CoatSampleWeight(const SurfacePayload& material) {
    return fmaxf(0.0f, MaxComponent(CoatF0(material)));
}

static __forceinline__ __device__ float TransmissionSampleWeight(const SurfacePayload& material) {
    const float transmission = Clamp(material.transmission, 0.0f, 1.0f);
    const float metallic = Clamp(material.metallic, 0.0f, 1.0f);
    if (transmission <= 0.0f || metallic >= 0.999f) {
        return 0.0f;
    }

    const OptixVec3f underCoat = CoatUnderlayerTint(material) * OneMinusClamped(CoatF0(material));
    const OptixVec3f underBase = OneMinusClamped(SpecularF0(material));
    return fmaxf(0.0f, MaxComponent(TransmissionTint(material) * underCoat * underBase))
        * transmission * (1.0f - metallic);
}

static __forceinline__ __device__ OptixVec3f TransmissionTint(const SurfacePayload& material) {
    const float dispersion = Clamp(material.transmissionDispersion, 0.0f, 1.0f);
    return MakeVec3f(
        material.transmissionColor.x * (1.0f + 0.15f * dispersion),
        material.transmissionColor.y,
        material.transmissionColor.z * (1.0f + 0.15f * (1.0f - dispersion)));
}

static __forceinline__ __device__ OptixVec3f MediumExtinction(const SurfacePayload& material) {
    const float depth = fmaxf(material.transmissionDepth, 1.0e-3f);
    const OptixVec3f tint = MakeVec3f(
        Clamp(material.transmissionColor.x, 0.0f, 1.0f),
        Clamp(material.transmissionColor.y, 0.0f, 1.0f),
        Clamp(material.transmissionColor.z, 0.0f, 1.0f));
    const OptixVec3f scatter = MakeVec3f(
        Clamp(material.transmissionScatter.x, 0.0f, 1.0f),
        Clamp(material.transmissionScatter.y, 0.0f, 1.0f),
        Clamp(material.transmissionScatter.z, 0.0f, 1.0f));
    const float scatterScale = 1.0f - 0.35f * Clamp(material.transmissionScatterAnisotropy, -0.95f, 0.95f);
    return MakeVec3f(
        (1.0f - tint.x + scatter.x * scatterScale) / depth,
        (1.0f - tint.y + scatter.y * scatterScale) / depth,
        (1.0f - tint.z + scatter.z * scatterScale) / depth);
}

static __forceinline__ __device__ OptixVec3f MediumAttenuation(const OptixVec3f& extinction, float distance) {
    return Exp(MakeVec3f(-extinction.x * distance, -extinction.y * distance, -extinction.z * distance));
}

static __forceinline__ __device__ OptixVec3f LoadPixel(
    const OptixEnvironmentMapData& environment,
    uint32_t x,
    uint32_t y) {
    if (!environment.pixels || environment.width == 0u || environment.height == 0u) {
        return MakeVec3f(0.0f, 0.0f, 0.0f);
    }

    const uint32_t wrappedX = environment.width > 0u ? (x % environment.width) : 0u;
    const uint32_t clampedY = environment.height > 0u ? min(y, environment.height - 1u) : 0u;
    return environment.pixels[static_cast<unsigned long long>(clampedY) * environment.width + wrappedX];
}

static __forceinline__ __device__ OptixVec3f BilinearSample(const OptixEnvironmentMapData& environment, float u, float v) {
    if (!environment.pixels || environment.width == 0u || environment.height == 0u) {
        return MakeVec3f(0.0f, 0.0f, 0.0f);
    }

    u = WrapUnit(u) * static_cast<float>(environment.width) - 0.5f;
    v = Clamp(v, 0.0f, 1.0f) * static_cast<float>(environment.height) - 0.5f;

    const int x0 = static_cast<int>(floorf(u));
    const int y0 = static_cast<int>(floorf(v));
    const int x1 = x0 + 1;
    const int y1 = min(y0 + 1, static_cast<int>(environment.height) - 1);
    const float tx = u - static_cast<float>(x0);
    const float ty = v - static_cast<float>(y0);

    const auto wrapX = [&](int x) {
        const int width = static_cast<int>(environment.width);
        int wrapped = x % width;
        return wrapped < 0 ? wrapped + width : wrapped;
    };
    const auto clampY = [&](int y) {
        return max(0, min(y, static_cast<int>(environment.height) - 1));
    };

    const OptixVec3f c00 = LoadPixel(environment, static_cast<uint32_t>(wrapX(x0)), static_cast<uint32_t>(clampY(y0)));
    const OptixVec3f c10 = LoadPixel(environment, static_cast<uint32_t>(wrapX(x1)), static_cast<uint32_t>(clampY(y0)));
    const OptixVec3f c01 = LoadPixel(environment, static_cast<uint32_t>(wrapX(x0)), static_cast<uint32_t>(clampY(y1)));
    const OptixVec3f c11 = LoadPixel(environment, static_cast<uint32_t>(wrapX(x1)), static_cast<uint32_t>(clampY(y1)));
    const OptixVec3f c0 = Lerp(c00, c10, tx);
    const OptixVec3f c1 = Lerp(c01, c11, tx);
    return Lerp(c0, c1, ty);
}

static __forceinline__ __device__ OptixVec3f EvaluateEnvironmentMap(
    const OptixEnvironmentMapData& environment,
    const OptixVec3f& localDirection) {
    if (!environment.pixels || environment.width == 0u || environment.height == 0u) {
        return MakeVec3f(1.0f, 1.0f, 1.0f);
    }

    const OptixVec3f direction = Normalize(localDirection);
    if (environment.layout == kEnvironmentLayoutAngular) {
        const float radialLength = sqrtf(direction.x * direction.x + direction.y * direction.y);
        if (radialLength <= 1.0e-6f) {
            return BilinearSample(environment, 0.5f, 0.5f);
        }

        const float angle = acosf(Clamp(direction.z, -1.0f, 1.0f));
        const float radius = angle / kPi;
        const float normalizedX = direction.x / radialLength;
        const float normalizedY = direction.y / radialLength;
        const float u = 0.5f + 0.5f * radius * normalizedX;
        const float v = 0.5f + 0.5f * radius * normalizedY;
        if (u < 0.0f || u > 1.0f || v < 0.0f || v > 1.0f) {
            return MakeVec3f(0.0f, 0.0f, 0.0f);
        }
        return BilinearSample(environment, u, v);
    }

    const float phi = atan2f(direction.x, direction.z);
    const float theta = acosf(Clamp(direction.y, -1.0f, 1.0f));
    const float u = WrapUnit(phi / (2.0f * kPi));
    const float v = Clamp(theta / kPi, 0.0f, 1.0f);
    return BilinearSample(environment, u, v);
}

static __forceinline__ __device__ float EvaluateEnvironmentMapPdf(
    const OptixEnvironmentMapData& environment,
    const OptixVec3f& localDirection) {
    if (!environment.pixels || environment.width == 0u || environment.height == 0u) {
        return 0.0f;
    }

    if (environment.layout != kEnvironmentLayoutLatLong
        || environment.hasImportance == 0u
        || !environment.rowCdf
        || !environment.conditionalCdf) {
        return kInvFourPi;
    }

    const OptixVec3f direction = Normalize(localDirection);
    const float phi = atan2f(direction.x, direction.z);
    const float theta = acosf(Clamp(direction.y, -1.0f, 1.0f));
    const float u = WrapUnit(phi / (2.0f * kPi));
    const float v = Clamp(theta / kPi, 0.0f, 1.0f);
    const uint32_t x = min(static_cast<uint32_t>(u * static_cast<float>(environment.width)), environment.width - 1u);
    const uint32_t y = min(static_cast<uint32_t>(v * static_cast<float>(environment.height)), environment.height - 1u);
    const float sinTheta = sinf(theta);
    if (sinTheta <= 1.0e-6f) {
        return 0.0f;
    }

    const float rowMass = environment.rowCdf[y + 1u] - environment.rowCdf[y];
    const uint32_t conditionalOffset = y * (environment.width + 1u);
    const float columnMass = environment.conditionalCdf[conditionalOffset + x + 1u]
        - environment.conditionalCdf[conditionalOffset + x];
    const float pixelMass = rowMass * columnMass;
    return pixelMass * static_cast<float>(environment.width * environment.height)
        / (2.0f * kPi * kPi * sinTheta);
}

static __forceinline__ __device__ uint32_t SampleCdfIndex(
    const float* cdf,
    uint32_t begin,
    uint32_t end,
    float sample,
    float* pdfMass) {
    if (!cdf || end <= begin) {
        if (pdfMass) {
            *pdfMass = 0.0f;
        }
        return 0u;
    }

    const float target = Clamp(sample, 0.0f, 0.99999994f);
    uint32_t low = begin;
    uint32_t high = end;
    while (low + 1u < high) {
        const uint32_t mid = (low + high) / 2u;
        if (cdf[mid] <= target) {
            low = mid;
        } else {
            high = mid;
        }
    }

    if (pdfMass) {
        *pdfMass = cdf[low + 1u] - cdf[low];
    }
    return low - begin;
}

static __forceinline__ __device__ OptixVec3f WorldToLight(const OptixDomeLight& light, const OptixVec3f& direction) {
    return Normalize({
        Dot(direction, light.right),
        Dot(direction, light.up),
        Dot(direction, light.forward),
    });
}

static __forceinline__ __device__ OptixVec3f LightToWorld(const OptixDomeLight& light, const OptixVec3f& direction) {
    return Normalize(light.right * direction.x + light.up * direction.y + light.forward * direction.z);
}

static __forceinline__ __device__ OptixVec3f EvaluateDomeLight(const OptixDomeLight& light, const OptixVec3f& direction) {
    if (light.environment.pixels) {
        return EvaluateEnvironmentMap(light.environment, WorldToLight(light, direction)) * light.radiance;
    }
    return light.radiance;
}

static __forceinline__ __device__ float EvaluateDomeLightPdf(
    const OptixDomeLight& light,
    const OptixVec3f& direction) {
    if (light.environment.pixels) {
        return EvaluateEnvironmentMapPdf(light.environment, WorldToLight(light, direction));
    }
    return kInvFourPi;
}

static __forceinline__ __device__ bool SampleDomeLight(
    const OptixDomeLight& light,
    SampleStream& stream,
    OptixVec3f* direction,
    OptixVec3f* radiance,
    float* pdf) {
    if (!direction || !radiance || !pdf) {
        return false;
    }

    OptixVec3f localDirection{};
    float localPdf = 0.0f;
    if (light.environment.pixels
        && light.environment.layout == kEnvironmentLayoutLatLong
        && light.environment.hasImportance != 0u
        && light.environment.rowCdf
        && light.environment.conditionalCdf
        && light.environment.width > 0u
        && light.environment.height > 0u) {
        const uint32_t row = SampleCdfIndex(
            light.environment.rowCdf,
            0u,
            light.environment.height,
            RandomFloat(stream),
            &localPdf);
        const uint32_t conditionalOffset = row * (light.environment.width + 1u);
        float columnMass = 0.0f;
        const uint32_t column = SampleCdfIndex(
            light.environment.conditionalCdf,
            conditionalOffset,
            conditionalOffset + light.environment.width,
            RandomFloat(stream),
            &columnMass);

        const float u = (static_cast<float>(column) + 0.5f) / static_cast<float>(light.environment.width);
        const float v = (static_cast<float>(row) + 0.5f) / static_cast<float>(light.environment.height);
        const float theta = v * kPi;
        const float phi = u * 2.0f * kPi;
        const float sinTheta = sinf(theta);
        localDirection = {
            sinf(phi) * sinTheta,
            cosf(theta),
            cosf(phi) * sinTheta,
        };
        localPdf = sinTheta > 1.0e-6f
            ? localPdf * columnMass * static_cast<float>(light.environment.width * light.environment.height)
                / (2.0f * kPi * kPi * sinTheta)
            : 0.0f;
    } else {
        localDirection = UniformSampleSphere(stream);
        localPdf = kInvFourPi;
    }

    if (localPdf <= 0.0f) {
        return false;
    }

    *direction = LightToWorld(light, localDirection);
    *radiance = light.environment.pixels
        ? EvaluateEnvironmentMap(light.environment, localDirection) * light.radiance
        : light.radiance;
    *pdf = localPdf;
    return true;
}

static __forceinline__ __device__ OptixVec3f EnvironmentRadiance(const OptixVec3f& direction) {
    if (params.domeLightCount > 0u && params.domeLights) {
        OptixVec3f radiance = MakeVec3f(0.0f, 0.0f, 0.0f);
        for (uint32_t lightIndex = 0; lightIndex < params.domeLightCount; ++lightIndex) {
            radiance = Add(radiance, EvaluateDomeLight(params.domeLights[lightIndex], direction));
        }
        return radiance;
    }

    const float t = Clamp(direction.y * 0.5f + 0.5f, 0.0f, 1.0f);
    return Lerp(params.environmentBottom, params.environmentTop, t);
}

static __forceinline__ __device__ Ray GenerateCameraRay(
    uint32_t x,
    uint32_t y,
    SampleStream& stream) {
    const float sampleX = RandomFloat(stream);
    const float sampleY = RandomFloat(stream);
    const float filmX = ((static_cast<float>(x) + sampleX) / static_cast<float>(params.width)) * 2.0f - 1.0f;
    const float filmY = ((static_cast<float>(y) + sampleY) / static_cast<float>(params.height)) * 2.0f - 1.0f;
    const float tanHalfFov = tanf(params.verticalFovDegrees * 0.5f * 0.017453292519943295f);

    Ray ray;
    ray.origin = params.cameraPosition;
    ray.direction = Normalize(Add(
        Add(
            params.cameraForward,
            Mul(params.cameraRight, filmX * params.aspectRatio * tanHalfFov)),
        Mul(params.cameraUp, filmY * tanHalfFov)));
    return ray;
}

static __forceinline__ __device__ SurfacePayload TraceSurface(const Ray& ray) {
    SurfacePayload payload{};
    if (params.traversable == 0ull) {
        return payload;
    }

    unsigned int payload0 = 0u;
    unsigned int payload1 = 0u;
    PackPointer(&payload, payload0, payload1);

    optixTrace(
        static_cast<OptixTraversableHandle>(params.traversable),
        make_float3(ray.origin.x, ray.origin.y, ray.origin.z),
        make_float3(ray.direction.x, ray.direction.y, ray.direction.z),
        kRayEpsilon,
        kInfinity,
        0.0f,
        OptixVisibilityMask(0xff),
        OPTIX_RAY_FLAG_NONE,
        0,
        1,
        0,
        payload0,
        payload1);
    return payload;
}

static __forceinline__ __device__ bool IsOccluded(const OptixVec3f& origin, const OptixVec3f& direction) {
    if (params.traversable == 0ull) {
        return false;
    }

    unsigned int visible = 0u;
    unsigned int payload1 = 0u;
    optixTrace(
        static_cast<OptixTraversableHandle>(params.traversable),
        make_float3(origin.x, origin.y, origin.z),
        make_float3(direction.x, direction.y, direction.z),
        kRayEpsilon,
        kInfinity,
        0.0f,
        OptixVisibilityMask(0xff),
        OPTIX_RAY_FLAG_TERMINATE_ON_FIRST_HIT
            | OPTIX_RAY_FLAG_DISABLE_ANYHIT
            | OPTIX_RAY_FLAG_DISABLE_CLOSESTHIT,
        0,
        1,
        1,
        visible,
        payload1);
    return visible == 0u;
}

static __forceinline__ __device__ OptixVec3f DirectLighting(
    const OptixVec3f& position,
    const OptixVec3f& normal,
    const OptixVec3f& coatNormal,
    const OptixVec3f& viewDirection,
    const SurfacePayload& material,
    SampleStream& stream) {
    OptixVec3f radiance = MakeVec3f(0.0f, 0.0f, 0.0f);
    const OptixVec3f origin = Add(position, Mul(normal, kRayEpsilon));

    for (uint32_t lightIndex = 0; lightIndex < params.directionalLightCount; ++lightIndex) {
        const OptixDirectionalLight& light = params.directionalLights[lightIndex];
        const OptixVec3f lightDirection = Normalize(Mul(light.direction, -1.0f));
        const float nDotL = Clamp(Dot(normal, lightDirection), 0.0f, 1.0f);
        if (nDotL <= 0.0f || IsOccluded(origin, lightDirection)) {
            continue;
        }

        radiance = Add(
            radiance,
            ClampMaxComponentValue(
                EvaluateBsdf(material, normal, coatNormal, viewDirection, lightDirection) * light.radiance * nDotL,
                kDirectSampleClamp));
    }

    const uint32_t domeSamples = params.domeLightSamples > 0u ? params.domeLightSamples : 1u;
    for (uint32_t lightIndex = 0; lightIndex < params.domeLightCount; ++lightIndex) {
        const OptixDomeLight& light = params.domeLights[lightIndex];
        OptixVec3f lightContribution = MakeVec3f(0.0f, 0.0f, 0.0f);
        for (uint32_t sampleIndex = 0; sampleIndex < domeSamples; ++sampleIndex) {
            OptixVec3f lightDirection{};
            OptixVec3f lightRadiance{};
            float lightPdf = 0.0f;
            if (!SampleDomeLight(light, stream, &lightDirection, &lightRadiance, &lightPdf) || lightPdf <= 0.0f) {
                continue;
            }

            const float nDotL = Clamp(Dot(normal, lightDirection), 0.0f, 1.0f);
            if (nDotL <= 0.0f || IsOccluded(origin, lightDirection)) {
                continue;
            }

            lightContribution = Add(
                lightContribution,
                ClampMaxComponentValue(
                    EvaluateBsdf(material, normal, coatNormal, viewDirection, lightDirection) * lightRadiance
                        * (nDotL * PowerHeuristic(lightPdf, EvaluateBsdfPdf(
                            material,
                            normal,
                            coatNormal,
                            viewDirection,
                            lightDirection)) / lightPdf),
                    kDirectSampleClamp));
        }
        radiance = Add(radiance, lightContribution / static_cast<float>(domeSamples));
    }

    return radiance;
}

static __forceinline__ __device__ SampleResult TracePath(
    uint32_t x,
    uint32_t y,
    SampleStream& stream) {
    SampleResult result{};
    result.radiance = MakeVec3f(0.0f, 0.0f, 0.0f);
    result.albedo = MakeVec3f(0.0f, 0.0f, 0.0f);
    result.normal = MakeVec3f(0.0f, 0.0f, 0.0f);
    result.depth = kInfinity;

    Ray ray = GenerateCameraRay(x, y, stream);
    OptixVec3f throughput = MakeVec3f(1.0f, 1.0f, 1.0f);
    uint32_t diffuseDepth = 0u;
    uint32_t specularDepth = 0u;
    bool lastBounceSpecular = true;
    bool insideMedium = false;
    bool mediumActive = false;
    OptixVec3f mediumExtinction = MakeVec3f(0.0f, 0.0f, 0.0f);

    for (uint32_t depth = 0; depth < params.maxDepth; ++depth) {
        const SurfacePayload material = TraceSurface(ray);
        if (material.hit == 0u) {
            if (params.domeLightCount == 0u || depth == 0u || lastBounceSpecular) {
                if (depth > 0u || params.backgroundVisible != 0u) {
                    result.radiance = Add(result.radiance, Mul(throughput, EnvironmentRadiance(ray.direction)));
                }
            }
            break;
        }

        if (mediumActive) {
            throughput = Mul(throughput, MediumAttenuation(mediumExtinction, material.distance));
        }

        const bool frontFace = Dot(material.geometricNormal, ray.direction) < 0.0f;
        const OptixVec3f geometricNormal = frontFace ? material.geometricNormal : Mul(material.geometricNormal, -1.0f);
        OptixVec3f shadingBaseNormal = frontFace ? material.normal : Mul(material.normal, -1.0f);
        if (Dot(shadingBaseNormal, geometricNormal) < 0.0f) {
            shadingBaseNormal = Mul(shadingBaseNormal, -1.0f);
        }
        OptixVec3f shadingNormal = ResolveShadingNormal(material, shadingBaseNormal, false);
        if (Dot(shadingNormal, geometricNormal) < 0.0f) {
            shadingNormal = Mul(shadingNormal, -1.0f);
        }
        const OptixVec3f coatNormal = ResolveShadingNormal(material, shadingNormal, true);

        const OptixVec3f hitPosition = Add(ray.origin, Mul(ray.direction, material.distance));
        const OptixVec3f viewDirection = Mul(ray.direction, -1.0f);
        const float opacity = Clamp(material.opacity, 0.0f, 1.0f);
        if (opacity < 0.999f) {
            if (RandomFloat(stream) > opacity) {
                ray.origin = Add(hitPosition, Mul(ray.direction, kRayEpsilon));
                continue;
            }
            throughput = Div(throughput, fmaxf(opacity, 1.0e-4f));
        }
        if (depth == 0u) {
            result.albedo = DiffuseColor(material);
            result.normal = Add(Mul(shadingNormal, 0.5f), MakeVec3f(0.5f, 0.5f, 0.5f));
            result.depth = material.distance;
        }

        if (material.emissionStrength > 0.0f && (depth == 0u || lastBounceSpecular)) {
            result.radiance = Add(
                result.radiance,
                Mul(throughput, Mul(material.emissionColor, material.emissionStrength)));
        }

        result.radiance = Add(
            result.radiance,
            Mul(throughput, DirectLighting(hitPosition, shadingNormal, coatNormal, viewDirection, material, stream)));

        const float diffuseWeight = DiffuseSampleWeight(material);
        const float baseSpecularWeight = BaseSpecularSampleWeight(material);
        const float coatWeight = CoatSampleWeight(material);
        const float transmissionWeight = TransmissionSampleWeight(material);
        const float reflectiveWeight = diffuseWeight + baseSpecularWeight + coatWeight;
        const float totalWeight = reflectiveWeight + transmissionWeight;
        if (totalWeight <= 0.0f) {
            break;
        }

        const float selector = RandomFloat(stream) * totalWeight;
        OptixVec3f nextDirection = MakeVec3f(0.0f, 0.0f, 0.0f);
        OptixVec3f bsdfValue = MakeVec3f(0.0f, 0.0f, 0.0f);
        float pdf = 1.0f;
        bool chooseTransmission = false;
        bool chooseBaseSpecular = false;
        bool chooseCoat = false;

        if (selector < transmissionWeight) {
            chooseTransmission = true;
        } else {
            const float reflectiveSelector = selector - transmissionWeight;
            if (reflectiveSelector < diffuseWeight) {
            } else if (reflectiveSelector < diffuseWeight + baseSpecularWeight) {
                chooseBaseSpecular = true;
            } else {
                chooseCoat = true;
            }
        }

        if (chooseTransmission) {
            if (specularDepth >= params.specularDepth) {
                break;
            }
            ++specularDepth;

            const float transmissionProbability = fmaxf(transmissionWeight / totalWeight, 1.0e-6f);
            const float transmissionRoughness = material.thinWalled != 0u
                ? 0.0f
                : AdjustedRoughness(
                    TransmissionRoughness(material),
                    material.specularAnisotropy,
                    material.specularRotation);
            if (material.thinWalled != 0u) {
                nextDirection = ray.direction;
                const OptixVec3f transmissionScale = TransmissionTint(material)
                    * CoatTransmissionScale(material, coatNormal, viewDirection, nextDirection)
                    * (Clamp(material.transmission, 0.0f, 1.0f) * (1.0f - Clamp(material.metallic, 0.0f, 1.0f)));
                const float absCosTheta = fmaxf(fabsf(Dot(geometricNormal, nextDirection)), 1.0e-4f);
                bsdfValue = transmissionScale / absCosTheta;
                pdf = transmissionProbability;
            } else {
                const OptixVec3f microNormal = GgxSampleHalfVector(geometricNormal, stream, transmissionRoughness);
                const float etaI = insideMedium ? fmaxf(material.ior, 1.0f) : 1.0f;
                const float etaT = insideMedium ? 1.0f : fmaxf(material.ior, 1.0f);
                const float etaRatio = etaI / etaT;
                const float eta = etaT / etaI;
                if (!RefractDirection(ray.direction, microNormal, etaRatio, &nextDirection)) {
                    nextDirection = Normalize(Reflect(ray.direction, microNormal));
                    if (reflectiveWeight <= 0.0f) {
                        break;
                    }
                    bsdfValue = EvaluateBsdf(material, shadingNormal, coatNormal, viewDirection, nextDirection);
                    pdf = fmaxf(
                        (reflectiveWeight / totalWeight)
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
                    pdf = fmaxf(
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
                        mediumActive = material.transmissionDepth > 0.0f
                            || MaxComponent(material.transmissionScatter) > 0.0f;
                    } else {
                        mediumActive = false;
                    }
                }
            }
        } else if (chooseBaseSpecular || chooseCoat) {
            if (specularDepth >= params.specularDepth) {
                break;
            }
            ++specularDepth;
            const OptixVec3f sampleNormal = chooseCoat ? coatNormal : shadingNormal;
            const float sampleRoughness = chooseCoat
                ? AdjustedRoughness(material.coatRoughness, material.coatAnisotropy, material.coatRotation)
                : AdjustedRoughness(BaseLayerRoughness(material), material.specularAnisotropy, material.specularRotation);
            const OptixVec3f halfVector = GgxSampleHalfVector(sampleNormal, stream, sampleRoughness);
            nextDirection = Normalize(Reflect(ray.direction, halfVector));
            const float reflectionCosTheta = Clamp(Dot(sampleNormal, nextDirection), 0.0f, 1.0f);
            if (reflectionCosTheta <= 0.0f) {
                break;
            }
            pdf = fmaxf(
                (reflectiveWeight / totalWeight)
                    * EvaluateBsdfPdf(material, shadingNormal, coatNormal, viewDirection, nextDirection),
                1.0e-6f);
            bsdfValue = EvaluateBsdf(material, shadingNormal, coatNormal, viewDirection, nextDirection);
        } else {
            if (diffuseDepth >= params.diffuseDepth) {
                break;
            }
            ++diffuseDepth;
            nextDirection = CosineSampleHemisphere(shadingNormal, stream);
            pdf = fmaxf(
                (reflectiveWeight / totalWeight)
                    * EvaluateBsdfPdf(material, shadingNormal, coatNormal, viewDirection, nextDirection),
                1.0e-6f);
            bsdfValue = EvaluateBsdf(material, shadingNormal, coatNormal, viewDirection, nextDirection);
        }

        const float cosTheta = chooseTransmission
            ? fmaxf(fabsf(Dot(geometricNormal, nextDirection)), 1.0e-4f)
            : Clamp(Dot(shadingNormal, nextDirection), 0.0f, 1.0f);
        if (cosTheta <= 0.0f) {
            break;
        }

        throughput = ClampMaxComponentValue(throughput * bsdfValue * (cosTheta / pdf), kThroughputClamp);
        const OptixVec3f rayNormal = chooseTransmission ? geometricNormal : shadingNormal;
        const float originOffset = Dot(rayNormal, nextDirection) >= 0.0f ? 1.0f : -1.0f;
        ray.origin = Add(hitPosition, Mul(rayNormal, kRayEpsilon * originOffset));
        ray.direction = nextDirection;
        lastBounceSpecular = chooseBaseSpecular || chooseCoat || chooseTransmission;

        if (depth >= 2u) {
            const float continueProbability = Clamp(MaxComponent(throughput), 0.05f, 0.95f);
            if (RandomFloat(stream) > continueProbability) {
                break;
            }
            throughput = Div(throughput, continueProbability);
        }
    }

    return result;
}

}  // namespace

extern "C" __global__ void __raygen__shiro() {
    const uint3 launchIndex = optixGetLaunchIndex();
    const uint3 launchDimensions = optixGetLaunchDimensions();
    if (launchIndex.x >= launchDimensions.x || launchIndex.y >= launchDimensions.y) {
        return;
    }

    const uint32_t pixelIndex = launchIndex.y * params.width + launchIndex.x;
    OptixVec3f radianceSum = MakeVec3f(0.0f, 0.0f, 0.0f);
    OptixVec3f albedoSum = MakeVec3f(0.0f, 0.0f, 0.0f);
    OptixVec3f normalSum = MakeVec3f(0.0f, 0.0f, 0.0f);
    float bestDepth = kInfinity;
    unsigned int hitCount = 0u;

    for (uint32_t sampleIndex = 0; sampleIndex < params.sampleCount; ++sampleIndex) {
        SampleStream stream = MakeSampleStream(pixelIndex, sampleIndex);
        const SampleResult sample = TracePath(launchIndex.x, launchIndex.y, stream);
        radianceSum = Add(radianceSum, sample.radiance);
        albedoSum = Add(albedoSum, sample.albedo);
        normalSum = Add(normalSum, sample.normal);
        if (sample.depth < kInfinity) {
            bestDepth = fminf(bestDepth, sample.depth);
            ++hitCount;
        }
    }

    const float inverseSampleCount = params.sampleCount > 0u
        ? 1.0f / static_cast<float>(params.sampleCount)
        : 0.0f;
    params.beauty[pixelIndex] = OptixVec4f{
        radianceSum.x * inverseSampleCount,
        radianceSum.y * inverseSampleCount,
        radianceSum.z * inverseSampleCount,
        hitCount > 0u ? 1.0f : 0.0f,
    };
    params.albedo[pixelIndex] = Mul(albedoSum, inverseSampleCount);
    params.normal[pixelIndex] = Mul(normalSum, inverseSampleCount);
    params.depth[pixelIndex] = hitCount > 0u ? bestDepth : kInfinity;
}

extern "C" __global__ void __miss__radiance() {
    SurfacePayload* payload = GetPayload<SurfacePayload>();
    if (!payload) {
        return;
    }

    payload->hit = 0u;
}

extern "C" __global__ void __miss__shadow() {
    optixSetPayload_0(1u);
}

extern "C" __global__ void __closesthit__shiro() {
    SurfacePayload* payload = GetPayload<SurfacePayload>();
    if (!payload) {
        return;
    }

    const HitGroupData* hitData = reinterpret_cast<const HitGroupData*>(optixGetSbtDataPointer());
    if (!hitData || !hitData->positions || !hitData->indices) {
        payload->hit = 0u;
        return;
    }

    const unsigned int primitiveIndex = optixGetPrimitiveIndex();
    const OptixUInt3 triangle = hitData->indices[primitiveIndex];
    const OptixVec3f p0 = hitData->positions[triangle.x];
    const OptixVec3f p1 = hitData->positions[triangle.y];
    const OptixVec3f p2 = hitData->positions[triangle.z];
    const float2 barycentrics = optixGetTriangleBarycentrics();
    const float b1 = barycentrics.x;
    const float b2 = barycentrics.y;
    const float b0 = 1.0f - b1 - b2;

    const OptixVec3f geometricNormal = Normalize(Cross(Sub(p1, p0), Sub(p2, p0)));
    OptixVec3f normal = geometricNormal;
    if (hitData->normals && hitData->normalCount > triangle.x && hitData->normalCount > triangle.y && hitData->normalCount > triangle.z) {
        const OptixVec3f n0 = hitData->normals[triangle.x];
        const OptixVec3f n1 = hitData->normals[triangle.y];
        const OptixVec3f n2 = hitData->normals[triangle.z];
        const OptixVec3f interpolated = Add(Add(Mul(n0, b0), Mul(n1, b1)), Mul(n2, b2));
        if (Length(interpolated) > 0.0f) {
            normal = Normalize(interpolated);
        }
    }

    payload->hit = 1u;
    payload->distance = optixGetRayTmax();
    payload->geometricNormal = geometricNormal;
    payload->normal = normal;
    payload->baseColor = hitData->baseColor;
    payload->specularColor = hitData->specularColor;
    payload->transmissionColor = hitData->transmissionColor;
    payload->transmissionScatter = hitData->transmissionScatter;
    payload->coatColor = hitData->coatColor;
    payload->subsurfaceColor = hitData->subsurfaceColor;
    payload->subsurfaceRadius = hitData->subsurfaceRadius;
    payload->sheenColor = hitData->sheenColor;
    payload->emissionColor = hitData->emissionColor;
    payload->normalOverride = hitData->normalOverride;
    payload->coatNormalOverride = hitData->coatNormalOverride;
    payload->tangentOverride = hitData->tangentOverride;
    payload->baseWeight = hitData->baseWeight;
    payload->emissionStrength = hitData->emissionStrength;
    payload->specularWeight = hitData->specularWeight;
    payload->metallic = hitData->metallic;
    payload->roughness = hitData->roughness;
    payload->diffuseRoughness = hitData->diffuseRoughness;
    payload->opacity = hitData->opacity;
    payload->specularAnisotropy = hitData->specularAnisotropy;
    payload->specularRotation = hitData->specularRotation;
    payload->coatWeight = hitData->coatWeight;
    payload->coatRoughness = hitData->coatRoughness;
    payload->coatIor = hitData->coatIor;
    payload->coatAnisotropy = hitData->coatAnisotropy;
    payload->coatRotation = hitData->coatRotation;
    payload->sheen = hitData->sheen;
    payload->sheenRoughness = hitData->sheenRoughness;
    payload->subsurface = hitData->subsurface;
    payload->subsurfaceScale = hitData->subsurfaceScale;
    payload->subsurfaceAnisotropy = hitData->subsurfaceAnisotropy;
    payload->transmission = hitData->transmission;
    payload->transmissionDepth = hitData->transmissionDepth;
    payload->transmissionScatterAnisotropy = hitData->transmissionScatterAnisotropy;
    payload->transmissionDispersion = hitData->transmissionDispersion;
    payload->transmissionExtraRoughness = hitData->transmissionExtraRoughness;
    payload->ior = hitData->ior;
    payload->coatAffectColor = hitData->coatAffectColor;
    payload->coatAffectRoughness = hitData->coatAffectRoughness;
    payload->thinFilmThickness = hitData->thinFilmThickness;
    payload->thinFilmIor = hitData->thinFilmIor;
    payload->thinWalled = hitData->thinWalled;
    payload->hasNormalOverride = hitData->hasNormalOverride;
    payload->hasCoatNormalOverride = hitData->hasCoatNormalOverride;
    payload->hasTangentOverride = hitData->hasTangentOverride;
}
