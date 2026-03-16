#pragma once

namespace shiro {
namespace backend {
namespace optix {

using OptixUInt32 = unsigned int;
using OptixUInt64 = unsigned long long;

struct OptixVec3f {
    float x;
    float y;
    float z;
};

struct OptixVec4f {
    float x;
    float y;
    float z;
    float w;
};

struct OptixUInt3 {
    OptixUInt32 x;
    OptixUInt32 y;
    OptixUInt32 z;
};

struct OptixDirectionalLight {
    OptixVec3f direction;
    float pad0 = 0.0f;
    OptixVec3f radiance;
    float pad1 = 0.0f;
};

struct OptixEnvironmentMapData {
    const OptixVec3f* pixels;
    const float* rowCdf;
    const float* conditionalCdf;
    OptixUInt32 width;
    OptixUInt32 height;
    OptixUInt32 layout;
    OptixUInt32 hasImportance;
};

struct OptixDomeLight {
    OptixVec3f radiance;
    float pad0 = 0.0f;
    OptixEnvironmentMapData environment;
    OptixVec3f right;
    float pad1 = 0.0f;
    OptixVec3f up;
    float pad2 = 0.0f;
    OptixVec3f forward;
    float pad3 = 0.0f;
};

struct HitGroupData {
    const OptixVec3f* positions;
    const OptixVec3f* normals;
    const OptixUInt3* indices;
    OptixUInt32 normalCount;
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
    OptixUInt32 thinWalled;
    OptixUInt32 hasNormalOverride;
    OptixUInt32 hasCoatNormalOverride;
    OptixUInt32 hasTangentOverride;
};

struct LaunchParams {
    OptixVec4f* beauty;
    OptixVec3f* albedo;
    OptixVec3f* normal;
    float* depth;
    const OptixDirectionalLight* directionalLights;
    const OptixDomeLight* domeLights;
    OptixUInt32 directionalLightCount;
    OptixUInt32 domeLightCount;
    OptixUInt32 domeLightSamples;
    OptixUInt32 width;
    OptixUInt32 height;
    OptixUInt32 sampleStart;
    OptixUInt32 sampleCount;
    OptixUInt32 maxDepth;
    OptixUInt32 diffuseDepth;
    OptixUInt32 specularDepth;
    OptixUInt32 backgroundVisible;
    OptixUInt64 traversable;
    OptixVec3f cameraPosition;
    OptixVec3f cameraForward;
    OptixVec3f cameraRight;
    OptixVec3f cameraUp;
    float verticalFovDegrees;
    float aspectRatio;
    OptixVec3f environmentTop;
    OptixVec3f environmentBottom;
};

}  // namespace optix
}  // namespace backend
}  // namespace shiro
