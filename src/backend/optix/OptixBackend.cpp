#include "shiro/backend/optix/OptixBackend.h"

#include "shiro/backend/optix/OptixLaunchParams.h"
#include "shiro/core/Config.h"
#include "shiro/render/EnvironmentMap.h"

#include <cstdio>
#include <cstring>
#include <fstream>
#include <limits>
#include <sstream>
#include <string>
#include <vector>

#if SHIRO_HAVE_CUDA && SHIRO_HAVE_OPTIX
#include <cuda.h>
#include <nvrtc.h>

#include <optix.h>
#include <optix_function_table_definition.h>
#include <optix_stack_size.h>
#include <optix_stubs.h>
#endif

namespace shiro::backend::optix {

using namespace shiro::render;

namespace {

#if SHIRO_HAVE_CUDA && SHIRO_HAVE_OPTIX

constexpr size_t kOptixLogBufferSize = 8192;

template <typename T>
struct SbtRecord {
    alignas(OPTIX_SBT_RECORD_ALIGNMENT) char header[OPTIX_SBT_RECORD_HEADER_SIZE];
    T data;
};

struct EmptyData {
};

bool CheckCuda(CUresult result, const char* context) {
    if (result == CUDA_SUCCESS) {
        return true;
    }

    const char* name = nullptr;
    const char* description = nullptr;
    cuGetErrorName(result, &name);
    cuGetErrorString(result, &description);
    std::fprintf(
        stderr,
        "Shiro OptiX CUDA failure at %s: %s (%s)\n",
        context ? context : "unknown",
        name ? name : "unknown",
        description ? description : "no description");
    return false;
}

bool CheckNvrtc(nvrtcResult result, const char* context) {
    if (result == NVRTC_SUCCESS) {
        return true;
    }

    std::fprintf(
        stderr,
        "Shiro OptiX NVRTC failure at %s: %s\n",
        context ? context : "unknown",
        nvrtcGetErrorString(result));
    return false;
}

bool CheckOptix(OptixResult result, const char* context) {
    if (result == OPTIX_SUCCESS) {
        return true;
    }

    std::fprintf(
        stderr,
        "Shiro OptiX failure at %s: %d\n",
        context ? context : "unknown",
        static_cast<int>(result));
    return false;
}

void PrintOptixLog(const char* context, const char* log, size_t logSize) {
    if (!log || logSize <= 1) {
        return;
    }

    const std::string message(log, log + logSize - 1u);
    if (!message.empty()) {
        std::fprintf(stderr, "Shiro OptiX log at %s:\n%s\n", context ? context : "unknown", message.c_str());
    }
}

std::string ReadTextFile(const char* path) {
    if (!path || *path == '\0') {
        return {};
    }

    std::ifstream input(path, std::ios::in | std::ios::binary);
    if (!input) {
        return {};
    }

    std::ostringstream buffer;
    buffer << input.rdbuf();
    return buffer.str();
}

OptixVec3f ToOptixVec3f(const Vec3f& value) {
    return {value.x, value.y, value.z};
}

OptixDirectionalLight ToOptixDirectionalLight(const DirectionalLight& light) {
    OptixDirectionalLight result{};
    result.direction = ToOptixVec3f(light.direction);
    result.radiance = ToOptixVec3f(light.radiance);
    return result;
}

bool MeshHasRenderableTriangles(const TriangleMesh& mesh) {
    return mesh.positions.size() >= 3
        && mesh.indices.size() >= 3
        && (mesh.indices.size() % 3u) == 0u;
}

template <typename T>
void HashBytes(uint64_t* hash, const T* data, size_t count) {
    if (!hash || !data || count == 0) {
        return;
    }

    const unsigned char* bytes = reinterpret_cast<const unsigned char*>(data);
    const size_t byteCount = sizeof(T) * count;
    for (size_t index = 0; index < byteCount; ++index) {
        *hash ^= static_cast<uint64_t>(bytes[index]);
        *hash *= 1099511628211ull;
    }
}

template <typename T>
void HashValue(uint64_t* hash, const T& value) {
    HashBytes(hash, &value, 1u);
}

uint64_t ComputeSceneSignature(const Scene& scene) {
    uint64_t hash = 1469598103934665603ull;

    HashValue(&hash, scene.materials.size());
    for (const PbrMaterial& material : scene.materials) {
        HashValue(&hash, material.baseColor);
        HashValue(&hash, material.emissionColor);
        HashValue(&hash, material.specularColor);
        HashValue(&hash, material.transmissionColor);
        HashValue(&hash, material.transmissionScatter);
        HashValue(&hash, material.coatColor);
        HashValue(&hash, material.subsurfaceColor);
        HashValue(&hash, material.subsurfaceRadius);
        HashValue(&hash, material.sheenColor);
        HashValue(&hash, material.normalOverride);
        HashValue(&hash, material.coatNormalOverride);
        HashValue(&hash, material.tangentOverride);
        HashValue(&hash, material.baseWeight);
        HashValue(&hash, material.specularWeight);
        HashValue(&hash, material.emissionStrength);
        HashValue(&hash, material.metallic);
        HashValue(&hash, material.roughness);
        HashValue(&hash, material.diffuseRoughness);
        HashValue(&hash, material.opacity);
        HashValue(&hash, material.specularAnisotropy);
        HashValue(&hash, material.specularRotation);
        HashValue(&hash, material.coatWeight);
        HashValue(&hash, material.coatRoughness);
        HashValue(&hash, material.coatIor);
        HashValue(&hash, material.coatAnisotropy);
        HashValue(&hash, material.coatRotation);
        HashValue(&hash, material.sheen);
        HashValue(&hash, material.sheenRoughness);
        HashValue(&hash, material.subsurface);
        HashValue(&hash, material.subsurfaceScale);
        HashValue(&hash, material.subsurfaceAnisotropy);
        HashValue(&hash, material.transmission);
        HashValue(&hash, material.transmissionDepth);
        HashValue(&hash, material.transmissionScatterAnisotropy);
        HashValue(&hash, material.transmissionDispersion);
        HashValue(&hash, material.transmissionExtraRoughness);
        HashValue(&hash, material.ior);
        HashValue(&hash, material.coatAffectColor);
        HashValue(&hash, material.coatAffectRoughness);
        HashValue(&hash, material.thinFilmThickness);
        HashValue(&hash, material.thinFilmIor);
        HashValue(&hash, material.thinWalled);
        HashValue(&hash, material.hasNormalOverride);
        HashValue(&hash, material.hasCoatNormalOverride);
        HashValue(&hash, material.hasTangentOverride);
    }

    HashValue(&hash, scene.meshes.size());
    for (const TriangleMesh& mesh : scene.meshes) {
        HashValue(&hash, mesh.materialIndex);
        HashValue(&hash, mesh.positions.size());
        HashBytes(&hash, mesh.positions.data(), mesh.positions.size());
        HashValue(&hash, mesh.normals.size());
        HashBytes(&hash, mesh.normals.data(), mesh.normals.size());
        HashValue(&hash, mesh.indices.size());
        HashBytes(&hash, mesh.indices.data(), mesh.indices.size());
    }

    HashValue(&hash, scene.distantLights.size());
    for (const DirectionalLight& light : scene.distantLights) {
        HashValue(&hash, light.direction);
        HashValue(&hash, light.radiance);
    }

    HashValue(&hash, scene.domeLights.size());
    for (const DomeLight& light : scene.domeLights) {
        HashValue(&hash, light.radiance);
        HashValue(&hash, light.layout);
        HashValue(&hash, light.right);
        HashValue(&hash, light.up);
        HashValue(&hash, light.forward);
        HashValue(&hash, light.textureFile.size());
        HashBytes(&hash, light.textureFile.data(), light.textureFile.size());
    }
    HashValue(&hash, scene.environmentTop);
    HashValue(&hash, scene.environmentBottom);
    return hash;
}

void FreeDevicePtr(CUdeviceptr* ptr) {
    if (!ptr || *ptr == 0) {
        return;
    }

    cuMemFree(*ptr);
    *ptr = 0;
}

void FreeDevicePtrVector(std::vector<CUdeviceptr>* buffers) {
    if (!buffers) {
        return;
    }

    for (CUdeviceptr& buffer : *buffers) {
        if (buffer != 0) {
            cuMemFree(buffer);
            buffer = 0;
        }
    }
    buffers->clear();
}

bool UploadDeviceBuffer(const void* data, size_t byteCount, CUdeviceptr* destination, const char* context) {
    if (!destination) {
        return false;
    }

    FreeDevicePtr(destination);
    if (byteCount == 0) {
        return true;
    }

    if (!CheckCuda(cuMemAlloc(destination, byteCount), context)) {
        return false;
    }
    if (data && !CheckCuda(cuMemcpyHtoD(*destination, data, byteCount), context)) {
        FreeDevicePtr(destination);
        return false;
    }

    return true;
}

template <typename T>
bool UploadVector(const std::vector<T>& data, CUdeviceptr* destination, const char* context) {
    return UploadDeviceBuffer(
        data.empty() ? nullptr : static_cast<const void*>(data.data()),
        data.size() * sizeof(T),
        destination,
        context);
}

std::string ComputeArchitectureOption(CUdevice device) {
    int major = 0;
    int minor = 0;
    if (!CheckCuda(cuDeviceGetAttribute(&major, CU_DEVICE_ATTRIBUTE_COMPUTE_CAPABILITY_MAJOR, device), "cuDeviceGetAttribute(major)")) {
        return "--gpu-architecture=compute_70";
    }
    if (!CheckCuda(cuDeviceGetAttribute(&minor, CU_DEVICE_ATTRIBUTE_COMPUTE_CAPABILITY_MINOR, device), "cuDeviceGetAttribute(minor)")) {
        return "--gpu-architecture=compute_70";
    }
    return "--gpu-architecture=compute_" + std::to_string(major) + std::to_string(minor);
}

std::string CompileOptixProgramSource(CUdevice device) {
    const std::string source = ReadTextFile(SHIRO_OPTIX_PROGRAM_SOURCE_PATH);
    if (source.empty()) {
        return {};
    }

    nvrtcProgram program = nullptr;
    if (!CheckNvrtc(nvrtcCreateProgram(
            &program,
            source.c_str(),
            "OptixPrograms.cu",
            0,
            nullptr,
            nullptr), "nvrtcCreateProgram")) {
        return {};
    }

    std::vector<std::string> optionStorage = {
        "--std=c++14",
        "--device-as-default-execution-space",
        "--use_fast_math",
        ComputeArchitectureOption(device),
        std::string("--include-path=") + SHIRO_OPTIX_INCLUDE_DIR,
        std::string("--include-path=") + SHIRO_CUDA_INCLUDE_DIR,
        std::string("--include-path=") + SHIRO_PROJECT_INCLUDE_DIR,
    };
    std::vector<const char*> options;
    options.reserve(optionStorage.size());
    for (const std::string& option : optionStorage) {
        options.push_back(option.c_str());
    }

    const nvrtcResult compileResult =
        nvrtcCompileProgram(program, static_cast<int>(options.size()), options.data());

    size_t logSize = 0;
    nvrtcGetProgramLogSize(program, &logSize);
    if (logSize > 1) {
        std::string log(logSize, '\0');
        nvrtcGetProgramLog(program, log.data());
        if (compileResult != NVRTC_SUCCESS) {
            std::fprintf(stderr, "Shiro OptiX NVRTC log:\n%s\n", log.c_str());
        }
    }

    if (!CheckNvrtc(compileResult, "nvrtcCompileProgram")) {
        nvrtcDestroyProgram(&program);
        return {};
    }

    size_t ptxSize = 0;
    if (!CheckNvrtc(nvrtcGetPTXSize(program, &ptxSize), "nvrtcGetPTXSize") || ptxSize == 0) {
        nvrtcDestroyProgram(&program);
        return {};
    }

    std::string ptx(ptxSize, '\0');
    if (!CheckNvrtc(nvrtcGetPTX(program, ptx.data()), "nvrtcGetPTX")) {
        nvrtcDestroyProgram(&program);
        return {};
    }

    nvrtcDestroyProgram(&program);
    return ptx;
}

#endif

}  // namespace

struct OptixBackend::RuntimeState {
#if SHIRO_HAVE_CUDA && SHIRO_HAVE_OPTIX
    CUdevice device = 0;
    CUcontext cudaContext = nullptr;
    CUstream stream = nullptr;
    bool usesPrimaryContext = false;

    OptixDeviceContext optixContext = nullptr;
    OptixModule module = nullptr;
    OptixProgramGroup raygenProgram = nullptr;
    OptixProgramGroup missProgram = nullptr;
    OptixProgramGroup shadowMissProgram = nullptr;
    OptixProgramGroup hitgroupProgram = nullptr;
    OptixPipeline pipeline = nullptr;
    OptixShaderBindingTable sbt{};

    CUdeviceptr raygenRecord = 0;
    CUdeviceptr missRecords = 0;
    CUdeviceptr hitgroupRecords = 0;
    CUdeviceptr launchParams = 0;

    CUdeviceptr beautyBuffer = 0;
    CUdeviceptr albedoBuffer = 0;
    CUdeviceptr normalBuffer = 0;
    CUdeviceptr depthBuffer = 0;
    uint32_t width = 0;
    uint32_t height = 0;

    CUdeviceptr directionalLights = 0;
    uint32_t directionalLightCount = 0;
    CUdeviceptr domeLights = 0;
    uint32_t domeLightCount = 0;

    OptixTraversableHandle gasHandle = 0;
    CUdeviceptr gasBuffer = 0;
    std::vector<CUdeviceptr> vertexBuffers;
    std::vector<CUdeviceptr> normalBuffers;
    std::vector<CUdeviceptr> indexBuffers;
    std::vector<CUdeviceptr> environmentPixelBuffers;
    std::vector<CUdeviceptr> environmentRowCdfBuffers;
    std::vector<CUdeviceptr> environmentConditionalCdfBuffers;
    uint64_t sceneSignature = 0;

    ~RuntimeState() {
        FreeDevicePtrVector(&environmentConditionalCdfBuffers);
        FreeDevicePtrVector(&environmentRowCdfBuffers);
        FreeDevicePtrVector(&environmentPixelBuffers);
        FreeDevicePtrVector(&vertexBuffers);
        FreeDevicePtrVector(&normalBuffers);
        FreeDevicePtrVector(&indexBuffers);
        FreeDevicePtr(&gasBuffer);
        FreeDevicePtr(&domeLights);
        FreeDevicePtr(&directionalLights);
        FreeDevicePtr(&depthBuffer);
        FreeDevicePtr(&normalBuffer);
        FreeDevicePtr(&albedoBuffer);
        FreeDevicePtr(&beautyBuffer);
        FreeDevicePtr(&launchParams);
        FreeDevicePtr(&hitgroupRecords);
        FreeDevicePtr(&missRecords);
        FreeDevicePtr(&raygenRecord);
        if (pipeline) {
            optixPipelineDestroy(pipeline);
        }
        if (hitgroupProgram) {
            optixProgramGroupDestroy(hitgroupProgram);
        }
        if (shadowMissProgram) {
            optixProgramGroupDestroy(shadowMissProgram);
        }
        if (missProgram) {
            optixProgramGroupDestroy(missProgram);
        }
        if (raygenProgram) {
            optixProgramGroupDestroy(raygenProgram);
        }
        if (module) {
            optixModuleDestroy(module);
        }
        if (optixContext) {
            optixDeviceContextDestroy(optixContext);
        }
        if (stream) {
            cuStreamDestroy(stream);
        }
        if (cudaContext) {
            if (usesPrimaryContext) {
                cuDevicePrimaryCtxRelease(device);
            } else {
                cuCtxDestroy(cudaContext);
            }
        }
    }
#endif
};

#if SHIRO_HAVE_CUDA && SHIRO_HAVE_OPTIX

namespace {

void ClearSceneData(OptixBackend::RuntimeState* runtime) {
    if (!runtime) {
        return;
    }

    FreeDevicePtrVector(&runtime->vertexBuffers);
    FreeDevicePtrVector(&runtime->normalBuffers);
    FreeDevicePtrVector(&runtime->indexBuffers);
    FreeDevicePtrVector(&runtime->environmentPixelBuffers);
    FreeDevicePtrVector(&runtime->environmentRowCdfBuffers);
    FreeDevicePtrVector(&runtime->environmentConditionalCdfBuffers);
    FreeDevicePtr(&runtime->gasBuffer);
    FreeDevicePtr(&runtime->domeLights);
    FreeDevicePtr(&runtime->directionalLights);
    FreeDevicePtr(&runtime->hitgroupRecords);
    runtime->directionalLightCount = 0;
    runtime->domeLightCount = 0;
    runtime->gasHandle = 0;
    runtime->sceneSignature = 0;
    runtime->sbt.hitgroupRecordBase = 0;
    runtime->sbt.hitgroupRecordStrideInBytes = 0;
    runtime->sbt.hitgroupRecordCount = 0;
}

bool EnsureOutputBuffers(OptixBackend::RuntimeState* runtime, uint32_t width, uint32_t height) {
    if (!runtime) {
        return false;
    }

    if (runtime->width == width
        && runtime->height == height
        && runtime->beautyBuffer != 0
        && runtime->albedoBuffer != 0
        && runtime->normalBuffer != 0
        && runtime->depthBuffer != 0) {
        return true;
    }

    FreeDevicePtr(&runtime->depthBuffer);
    FreeDevicePtr(&runtime->normalBuffer);
    FreeDevicePtr(&runtime->albedoBuffer);
    FreeDevicePtr(&runtime->beautyBuffer);

    const size_t pixelCount = static_cast<size_t>(width) * static_cast<size_t>(height);
    const size_t beautyBytes = pixelCount * sizeof(OptixVec4f);
    const size_t albedoBytes = pixelCount * sizeof(OptixVec3f);
    const size_t normalBytes = pixelCount * sizeof(OptixVec3f);
    const size_t depthBytes = pixelCount * sizeof(float);

    if (!UploadDeviceBuffer(nullptr, beautyBytes, &runtime->beautyBuffer, "cuMemAlloc(beautyBuffer)")) {
        return false;
    }
    if (!UploadDeviceBuffer(nullptr, albedoBytes, &runtime->albedoBuffer, "cuMemAlloc(albedoBuffer)")) {
        return false;
    }
    if (!UploadDeviceBuffer(nullptr, normalBytes, &runtime->normalBuffer, "cuMemAlloc(normalBuffer)")) {
        return false;
    }
    if (!UploadDeviceBuffer(nullptr, depthBytes, &runtime->depthBuffer, "cuMemAlloc(depthBuffer)")) {
        return false;
    }

    runtime->width = width;
    runtime->height = height;
    return true;
}

bool BuildPipeline(OptixBackend::RuntimeState* runtime) {
    if (!runtime) {
        return false;
    }

    const std::string ptx = CompileOptixProgramSource(runtime->device);
    if (ptx.empty()) {
        return false;
    }

    OptixModuleCompileOptions moduleOptions{};
    moduleOptions.maxRegisterCount = OPTIX_COMPILE_DEFAULT_MAX_REGISTER_COUNT;
    moduleOptions.optLevel = OPTIX_COMPILE_OPTIMIZATION_DEFAULT;
    moduleOptions.debugLevel = OPTIX_COMPILE_DEBUG_LEVEL_MINIMAL;

    OptixPipelineCompileOptions pipelineOptions{};
    pipelineOptions.usesMotionBlur = 0;
    pipelineOptions.traversableGraphFlags = OPTIX_TRAVERSABLE_GRAPH_FLAG_ALLOW_SINGLE_GAS;
    pipelineOptions.numPayloadValues = 2;
    pipelineOptions.numAttributeValues = 2;
    pipelineOptions.exceptionFlags = OPTIX_EXCEPTION_FLAG_NONE;
    pipelineOptions.pipelineLaunchParamsVariableName = "params";
    pipelineOptions.usesPrimitiveTypeFlags = OPTIX_PRIMITIVE_TYPE_FLAGS_TRIANGLE;

    char logBuffer[kOptixLogBufferSize];
    size_t logBufferSize = sizeof(logBuffer);
    const OptixResult moduleResult = optixModuleCreate(
        runtime->optixContext,
        &moduleOptions,
        &pipelineOptions,
        ptx.c_str(),
        ptx.size(),
        logBuffer,
        &logBufferSize,
        &runtime->module);
    PrintOptixLog("optixModuleCreate", logBuffer, logBufferSize);
    if (!CheckOptix(moduleResult, "optixModuleCreate")) {
        return false;
    }

    OptixProgramGroupOptions programGroupOptions{};

    OptixProgramGroupDesc raygenDesc{};
    raygenDesc.kind = OPTIX_PROGRAM_GROUP_KIND_RAYGEN;
    raygenDesc.raygen.module = runtime->module;
    raygenDesc.raygen.entryFunctionName = "__raygen__shiro";
    logBufferSize = sizeof(logBuffer);
    const OptixResult raygenResult = optixProgramGroupCreate(
        runtime->optixContext,
        &raygenDesc,
        1,
        &programGroupOptions,
        logBuffer,
        &logBufferSize,
        &runtime->raygenProgram);
    PrintOptixLog("optixProgramGroupCreate(raygen)", logBuffer, logBufferSize);
    if (!CheckOptix(raygenResult, "optixProgramGroupCreate(raygen)")) {
        return false;
    }

    OptixProgramGroupDesc missDesc{};
    missDesc.kind = OPTIX_PROGRAM_GROUP_KIND_MISS;
    missDesc.miss.module = runtime->module;
    missDesc.miss.entryFunctionName = "__miss__radiance";
    logBufferSize = sizeof(logBuffer);
    const OptixResult missResult = optixProgramGroupCreate(
        runtime->optixContext,
        &missDesc,
        1,
        &programGroupOptions,
        logBuffer,
        &logBufferSize,
        &runtime->missProgram);
    PrintOptixLog("optixProgramGroupCreate(miss)", logBuffer, logBufferSize);
    if (!CheckOptix(missResult, "optixProgramGroupCreate(miss)")) {
        return false;
    }

    OptixProgramGroupDesc shadowMissDesc{};
    shadowMissDesc.kind = OPTIX_PROGRAM_GROUP_KIND_MISS;
    shadowMissDesc.miss.module = runtime->module;
    shadowMissDesc.miss.entryFunctionName = "__miss__shadow";
    logBufferSize = sizeof(logBuffer);
    const OptixResult shadowMissResult = optixProgramGroupCreate(
        runtime->optixContext,
        &shadowMissDesc,
        1,
        &programGroupOptions,
        logBuffer,
        &logBufferSize,
        &runtime->shadowMissProgram);
    PrintOptixLog("optixProgramGroupCreate(shadowMiss)", logBuffer, logBufferSize);
    if (!CheckOptix(shadowMissResult, "optixProgramGroupCreate(shadowMiss)")) {
        return false;
    }

    OptixProgramGroupDesc hitgroupDesc{};
    hitgroupDesc.kind = OPTIX_PROGRAM_GROUP_KIND_HITGROUP;
    hitgroupDesc.hitgroup.moduleCH = runtime->module;
    hitgroupDesc.hitgroup.entryFunctionNameCH = "__closesthit__shiro";
    logBufferSize = sizeof(logBuffer);
    const OptixResult hitgroupResult = optixProgramGroupCreate(
        runtime->optixContext,
        &hitgroupDesc,
        1,
        &programGroupOptions,
        logBuffer,
        &logBufferSize,
        &runtime->hitgroupProgram);
    PrintOptixLog("optixProgramGroupCreate(hitgroup)", logBuffer, logBufferSize);
    if (!CheckOptix(hitgroupResult, "optixProgramGroupCreate(hitgroup)")) {
        return false;
    }

    OptixProgramGroup programGroups[] = {
        runtime->raygenProgram,
        runtime->missProgram,
        runtime->shadowMissProgram,
        runtime->hitgroupProgram,
    };

    OptixPipelineLinkOptions linkOptions{};
    linkOptions.maxTraceDepth = 1;

    logBufferSize = sizeof(logBuffer);
    const OptixResult pipelineResult = optixPipelineCreate(
        runtime->optixContext,
        &pipelineOptions,
        &linkOptions,
        programGroups,
        4u,
        logBuffer,
        &logBufferSize,
        &runtime->pipeline);
    PrintOptixLog("optixPipelineCreate", logBuffer, logBufferSize);
    if (!CheckOptix(pipelineResult, "optixPipelineCreate")) {
        return false;
    }

    OptixStackSizes stackSizes{};
    if (!CheckOptix(optixUtilAccumulateStackSizes(runtime->raygenProgram, &stackSizes, runtime->pipeline),
            "optixUtilAccumulateStackSizes(raygen)")) {
        return false;
    }
    if (!CheckOptix(optixUtilAccumulateStackSizes(runtime->missProgram, &stackSizes, runtime->pipeline),
            "optixUtilAccumulateStackSizes(miss)")) {
        return false;
    }
    if (!CheckOptix(optixUtilAccumulateStackSizes(runtime->shadowMissProgram, &stackSizes, runtime->pipeline),
            "optixUtilAccumulateStackSizes(shadowMiss)")) {
        return false;
    }
    if (!CheckOptix(optixUtilAccumulateStackSizes(runtime->hitgroupProgram, &stackSizes, runtime->pipeline),
            "optixUtilAccumulateStackSizes(hitgroup)")) {
        return false;
    }

    unsigned int directCallableStackSizeFromTraversal = 0u;
    unsigned int directCallableStackSizeFromState = 0u;
    unsigned int continuationStackSize = 0u;
    if (!CheckOptix(
            optixUtilComputeStackSizes(
                &stackSizes,
                linkOptions.maxTraceDepth,
                0u,
                0u,
                &directCallableStackSizeFromTraversal,
                &directCallableStackSizeFromState,
                &continuationStackSize),
            "optixUtilComputeStackSizes")) {
        return false;
    }

    if (!CheckOptix(
            optixPipelineSetStackSize(
                runtime->pipeline,
                directCallableStackSizeFromTraversal,
                directCallableStackSizeFromState,
                continuationStackSize,
                1),
            "optixPipelineSetStackSize")) {
        return false;
    }

    SbtRecord<EmptyData> raygenRecord{};
    if (!CheckOptix(optixSbtRecordPackHeader(runtime->raygenProgram, &raygenRecord), "optixSbtRecordPackHeader(raygen)")) {
        return false;
    }
    if (!UploadDeviceBuffer(&raygenRecord, sizeof(raygenRecord), &runtime->raygenRecord, "cuMemAlloc(raygenRecord)")) {
        return false;
    }

    std::vector<SbtRecord<EmptyData>> missRecords(2u);
    if (!CheckOptix(optixSbtRecordPackHeader(runtime->missProgram, &missRecords[0]), "optixSbtRecordPackHeader(miss)")) {
        return false;
    }
    if (!CheckOptix(optixSbtRecordPackHeader(runtime->shadowMissProgram, &missRecords[1]), "optixSbtRecordPackHeader(shadowMiss)")) {
        return false;
    }
    if (!UploadVector(missRecords, &runtime->missRecords, "cuMemAlloc(missRecords)")) {
        return false;
    }

    runtime->sbt.raygenRecord = runtime->raygenRecord;
    runtime->sbt.missRecordBase = runtime->missRecords;
    runtime->sbt.missRecordStrideInBytes = sizeof(SbtRecord<EmptyData>);
    runtime->sbt.missRecordCount = static_cast<unsigned int>(missRecords.size());

    if (!UploadDeviceBuffer(nullptr, sizeof(LaunchParams), &runtime->launchParams, "cuMemAlloc(launchParams)")) {
        return false;
    }

    return true;
}

bool UploadSceneData(OptixBackend::RuntimeState* runtime, const Scene& scene) {
    if (!runtime) {
        return false;
    }

    ClearSceneData(runtime);

    std::vector<OptixDirectionalLight> directionalLights;
    directionalLights.reserve(scene.distantLights.size());
    for (const DirectionalLight& light : scene.distantLights) {
        directionalLights.push_back(ToOptixDirectionalLight(light));
    }
    if (!UploadVector(directionalLights, &runtime->directionalLights, "cuMemAlloc(directionalLights)")) {
        return false;
    }
    runtime->directionalLightCount = static_cast<uint32_t>(directionalLights.size());

    std::vector<OptixDomeLight> domeLights;
    domeLights.reserve(scene.domeLights.size());
    runtime->environmentPixelBuffers.reserve(scene.domeLights.size());
    runtime->environmentRowCdfBuffers.reserve(scene.domeLights.size());
    runtime->environmentConditionalCdfBuffers.reserve(scene.domeLights.size());

    for (const DomeLight& light : scene.domeLights) {
        OptixDomeLight domeLight{};
        domeLight.radiance = ToOptixVec3f(light.radiance);
        domeLight.right = ToOptixVec3f(light.right);
        domeLight.up = ToOptixVec3f(light.up);
        domeLight.forward = ToOptixVec3f(light.forward);

        CUdeviceptr pixelBuffer = 0;
        CUdeviceptr rowCdfBuffer = 0;
        CUdeviceptr conditionalCdfBuffer = 0;
        if (light.environment) {
            std::vector<OptixVec3f> pixels;
            pixels.reserve(light.environment->Pixels().size());
            for (const Vec3f& pixel : light.environment->Pixels()) {
                pixels.push_back(ToOptixVec3f(pixel));
            }

            if (!UploadVector(pixels, &pixelBuffer, "cuMemAlloc(environmentPixels)")) {
                return false;
            }
            if (!UploadVector(light.environment->RowCdf(), &rowCdfBuffer, "cuMemAlloc(environmentRowCdf)")) {
                return false;
            }
            if (!UploadVector(
                    light.environment->ConditionalCdf(),
                    &conditionalCdfBuffer,
                    "cuMemAlloc(environmentConditionalCdf)")) {
                return false;
            }

            domeLight.environment.pixels =
                pixelBuffer != 0 ? reinterpret_cast<const OptixVec3f*>(pixelBuffer) : nullptr;
            domeLight.environment.rowCdf =
                rowCdfBuffer != 0 ? reinterpret_cast<const float*>(rowCdfBuffer) : nullptr;
            domeLight.environment.conditionalCdf =
                conditionalCdfBuffer != 0 ? reinterpret_cast<const float*>(conditionalCdfBuffer) : nullptr;
            domeLight.environment.width = light.environment->Width();
            domeLight.environment.height = light.environment->Height();
            domeLight.environment.layout = static_cast<uint32_t>(light.environment->Layout());
            domeLight.environment.hasImportance = !light.environment->RowCdf().empty() ? 1u : 0u;
        }

        runtime->environmentPixelBuffers.push_back(pixelBuffer);
        runtime->environmentRowCdfBuffers.push_back(rowCdfBuffer);
        runtime->environmentConditionalCdfBuffers.push_back(conditionalCdfBuffer);
        domeLights.push_back(domeLight);
    }

    if (!UploadVector(domeLights, &runtime->domeLights, "cuMemAlloc(domeLights)")) {
        return false;
    }
    runtime->domeLightCount = static_cast<uint32_t>(domeLights.size());

    std::vector<const TriangleMesh*> renderableMeshes;
    renderableMeshes.reserve(scene.meshes.size());
    for (const TriangleMesh& mesh : scene.meshes) {
        if (MeshHasRenderableTriangles(mesh)) {
            renderableMeshes.push_back(&mesh);
        }
    }

    if (renderableMeshes.empty()) {
        runtime->sceneSignature = ComputeSceneSignature(scene);
        return true;
    }

    runtime->vertexBuffers.reserve(renderableMeshes.size());
    runtime->normalBuffers.reserve(renderableMeshes.size());
    runtime->indexBuffers.reserve(renderableMeshes.size());

    std::vector<unsigned int> buildInputFlags(renderableMeshes.size(), OPTIX_GEOMETRY_FLAG_NONE);
    std::vector<OptixBuildInput> buildInputs(renderableMeshes.size());
    std::vector<SbtRecord<HitGroupData>> hitgroupRecords(renderableMeshes.size());
    std::vector<uint32_t> positionCounts(renderableMeshes.size(), 0u);
    std::vector<uint32_t> indexCounts(renderableMeshes.size(), 0u);
    std::vector<uint32_t> normalCounts(renderableMeshes.size(), 0u);

    for (size_t meshIndex = 0; meshIndex < renderableMeshes.size(); ++meshIndex) {
        const TriangleMesh& mesh = *renderableMeshes[meshIndex];

        std::vector<OptixVec3f> positions;
        positions.reserve(mesh.positions.size());
        for (const Vec3f& position : mesh.positions) {
            positions.push_back(ToOptixVec3f(position));
        }

        CUdeviceptr vertexBuffer = 0;
        if (!UploadVector(positions, &vertexBuffer, "cuMemAlloc(vertexBuffer)")) {
            return false;
        }
        runtime->vertexBuffers.push_back(vertexBuffer);
        positionCounts[meshIndex] = static_cast<uint32_t>(positions.size());

        CUdeviceptr normalBuffer = 0;
        if (mesh.normals.size() == mesh.positions.size()) {
            std::vector<OptixVec3f> normals;
            normals.reserve(mesh.normals.size());
            for (const Vec3f& normal : mesh.normals) {
                normals.push_back(ToOptixVec3f(normal));
            }
            if (!UploadVector(normals, &normalBuffer, "cuMemAlloc(normalBuffer)")) {
                return false;
            }
            normalCounts[meshIndex] = static_cast<uint32_t>(normals.size());
        }
        runtime->normalBuffers.push_back(normalBuffer);

        std::vector<OptixUInt3> indices;
        indices.reserve(mesh.indices.size() / 3u);
        for (size_t index = 0; index + 2u < mesh.indices.size(); index += 3u) {
            indices.push_back({
                mesh.indices[index + 0u],
                mesh.indices[index + 1u],
                mesh.indices[index + 2u],
            });
        }

        CUdeviceptr indexBuffer = 0;
        if (!UploadVector(indices, &indexBuffer, "cuMemAlloc(indexBuffer)")) {
            return false;
        }
        runtime->indexBuffers.push_back(indexBuffer);
        indexCounts[meshIndex] = static_cast<uint32_t>(indices.size());
    }

    for (size_t meshIndex = 0; meshIndex < renderableMeshes.size(); ++meshIndex) {
        const TriangleMesh& mesh = *renderableMeshes[meshIndex];
        OptixBuildInput& buildInput = buildInputs[meshIndex];
        buildInput.type = OPTIX_BUILD_INPUT_TYPE_TRIANGLES;
        buildInput.triangleArray.vertexFormat = OPTIX_VERTEX_FORMAT_FLOAT3;
        buildInput.triangleArray.vertexStrideInBytes = sizeof(OptixVec3f);
        buildInput.triangleArray.numVertices = positionCounts[meshIndex];
        buildInput.triangleArray.vertexBuffers = &runtime->vertexBuffers[meshIndex];
        buildInput.triangleArray.indexFormat = OPTIX_INDICES_FORMAT_UNSIGNED_INT3;
        buildInput.triangleArray.indexStrideInBytes = sizeof(OptixUInt3);
        buildInput.triangleArray.numIndexTriplets = indexCounts[meshIndex];
        buildInput.triangleArray.indexBuffer = runtime->indexBuffers[meshIndex];
        buildInput.triangleArray.flags = &buildInputFlags[meshIndex];
        buildInput.triangleArray.numSbtRecords = 1;

        const PbrMaterial material =
            mesh.materialIndex < scene.materials.size() ? scene.materials[mesh.materialIndex] : PbrMaterial{};

        SbtRecord<HitGroupData>& hitgroupRecord = hitgroupRecords[meshIndex];
        if (!CheckOptix(optixSbtRecordPackHeader(runtime->hitgroupProgram, &hitgroupRecord), "optixSbtRecordPackHeader(hitgroup)")) {
            return false;
        }
        hitgroupRecord.data.positions = reinterpret_cast<const OptixVec3f*>(runtime->vertexBuffers[meshIndex]);
        hitgroupRecord.data.normals = runtime->normalBuffers[meshIndex] != 0
            ? reinterpret_cast<const OptixVec3f*>(runtime->normalBuffers[meshIndex])
            : nullptr;
        hitgroupRecord.data.indices = reinterpret_cast<const OptixUInt3*>(runtime->indexBuffers[meshIndex]);
        hitgroupRecord.data.normalCount = normalCounts[meshIndex];
        hitgroupRecord.data.baseColor = ToOptixVec3f(material.baseColor);
        hitgroupRecord.data.specularColor = ToOptixVec3f(material.specularColor);
        hitgroupRecord.data.transmissionColor = ToOptixVec3f(material.transmissionColor);
        hitgroupRecord.data.transmissionScatter = ToOptixVec3f(material.transmissionScatter);
        hitgroupRecord.data.coatColor = ToOptixVec3f(material.coatColor);
        hitgroupRecord.data.subsurfaceColor = ToOptixVec3f(material.subsurfaceColor);
        hitgroupRecord.data.subsurfaceRadius = ToOptixVec3f(material.subsurfaceRadius);
        hitgroupRecord.data.sheenColor = ToOptixVec3f(material.sheenColor);
        hitgroupRecord.data.emissionColor = ToOptixVec3f(material.emissionColor);
        hitgroupRecord.data.normalOverride = ToOptixVec3f(material.normalOverride);
        hitgroupRecord.data.coatNormalOverride = ToOptixVec3f(material.coatNormalOverride);
        hitgroupRecord.data.tangentOverride = ToOptixVec3f(material.tangentOverride);
        hitgroupRecord.data.baseWeight = material.baseWeight;
        hitgroupRecord.data.emissionStrength = material.emissionStrength;
        hitgroupRecord.data.specularWeight = material.specularWeight;
        hitgroupRecord.data.metallic = material.metallic;
        hitgroupRecord.data.roughness = material.roughness;
        hitgroupRecord.data.diffuseRoughness = material.diffuseRoughness;
        hitgroupRecord.data.opacity = material.opacity;
        hitgroupRecord.data.specularAnisotropy = material.specularAnisotropy;
        hitgroupRecord.data.specularRotation = material.specularRotation;
        hitgroupRecord.data.coatWeight = material.coatWeight;
        hitgroupRecord.data.coatRoughness = material.coatRoughness;
        hitgroupRecord.data.coatIor = material.coatIor;
        hitgroupRecord.data.coatAnisotropy = material.coatAnisotropy;
        hitgroupRecord.data.coatRotation = material.coatRotation;
        hitgroupRecord.data.sheen = material.sheen;
        hitgroupRecord.data.sheenRoughness = material.sheenRoughness;
        hitgroupRecord.data.subsurface = material.subsurface;
        hitgroupRecord.data.subsurfaceScale = material.subsurfaceScale;
        hitgroupRecord.data.subsurfaceAnisotropy = material.subsurfaceAnisotropy;
        hitgroupRecord.data.transmission = material.transmission;
        hitgroupRecord.data.transmissionDepth = material.transmissionDepth;
        hitgroupRecord.data.transmissionScatterAnisotropy = material.transmissionScatterAnisotropy;
        hitgroupRecord.data.transmissionDispersion = material.transmissionDispersion;
        hitgroupRecord.data.transmissionExtraRoughness = material.transmissionExtraRoughness;
        hitgroupRecord.data.ior = material.ior;
        hitgroupRecord.data.coatAffectColor = material.coatAffectColor;
        hitgroupRecord.data.coatAffectRoughness = material.coatAffectRoughness;
        hitgroupRecord.data.thinFilmThickness = material.thinFilmThickness;
        hitgroupRecord.data.thinFilmIor = material.thinFilmIor;
        hitgroupRecord.data.thinWalled = material.thinWalled ? 1u : 0u;
        hitgroupRecord.data.hasNormalOverride = material.hasNormalOverride ? 1u : 0u;
        hitgroupRecord.data.hasCoatNormalOverride = material.hasCoatNormalOverride ? 1u : 0u;
        hitgroupRecord.data.hasTangentOverride = material.hasTangentOverride ? 1u : 0u;
    }

    OptixAccelBuildOptions accelOptions{};
    accelOptions.buildFlags = OPTIX_BUILD_FLAG_PREFER_FAST_TRACE;
    accelOptions.operation = OPTIX_BUILD_OPERATION_BUILD;

    OptixAccelBufferSizes accelSizes{};
    if (!CheckOptix(
            optixAccelComputeMemoryUsage(
                runtime->optixContext,
                &accelOptions,
                buildInputs.data(),
                static_cast<unsigned int>(buildInputs.size()),
                &accelSizes),
            "optixAccelComputeMemoryUsage")) {
        return false;
    }

    CUdeviceptr tempBuffer = 0;
    if (!UploadDeviceBuffer(nullptr, accelSizes.tempSizeInBytes, &tempBuffer, "cuMemAlloc(gasTempBuffer)")) {
        return false;
    }
    if (!UploadDeviceBuffer(nullptr, accelSizes.outputSizeInBytes, &runtime->gasBuffer, "cuMemAlloc(gasBuffer)")) {
        FreeDevicePtr(&tempBuffer);
        return false;
    }

    const OptixResult accelResult = optixAccelBuild(
        runtime->optixContext,
        runtime->stream,
        &accelOptions,
        buildInputs.data(),
        static_cast<unsigned int>(buildInputs.size()),
        tempBuffer,
        accelSizes.tempSizeInBytes,
        runtime->gasBuffer,
        accelSizes.outputSizeInBytes,
        &runtime->gasHandle,
        nullptr,
        0);
    FreeDevicePtr(&tempBuffer);
    if (!CheckOptix(accelResult, "optixAccelBuild")) {
        return false;
    }

    if (!UploadVector(hitgroupRecords, &runtime->hitgroupRecords, "cuMemAlloc(hitgroupRecords)")) {
        return false;
    }

    runtime->sbt.hitgroupRecordBase = runtime->hitgroupRecords;
    runtime->sbt.hitgroupRecordStrideInBytes = sizeof(SbtRecord<HitGroupData>);
    runtime->sbt.hitgroupRecordCount = static_cast<unsigned int>(hitgroupRecords.size());
    runtime->sceneSignature = ComputeSceneSignature(scene);
    return true;
}

}  // namespace

#endif

OptixBackend::OptixBackend() = default;
OptixBackend::~OptixBackend() = default;

BackendStatus OptixBackend::GetStatus() const {
    BackendStatus status;
    status.kind = BackendKind::Gpu;
    status.name = "optix";
    status.available =
#if SHIRO_HAVE_CUDA && SHIRO_HAVE_OPTIX
        true;
#else
        false;
#endif
    status.usesGpu = true;

    std::scoped_lock lock(mutex_);
#if SHIRO_HAVE_CUDA && SHIRO_HAVE_OPTIX
    if (!runtimeAttempted_) {
        int deviceCount = 0;
        if (cuInit(0) != CUDA_SUCCESS || cuDeviceGetCount(&deviceCount) != CUDA_SUCCESS || deviceCount <= 0) {
            status.available = false;
        }
    }
#endif
    if (runtimeAttempted_ && !runtime_) {
        status.available = false;
    }
    return status;
}

bool OptixBackend::SupportsScene(const Scene& scene) const {
    (void)scene;
#if !(SHIRO_HAVE_CUDA && SHIRO_HAVE_OPTIX)
    return false;
#else
    return true;
#endif
}

bool OptixBackend::EnsureRuntime(const RenderRequest& request) const {
#if !(SHIRO_HAVE_CUDA && SHIRO_HAVE_OPTIX)
    (void)request;
    return false;
#else
    std::scoped_lock lock(mutex_);
    runtimeAttempted_ = true;

    const uint64_t sceneSignature = ComputeSceneSignature(request.scene);
    if (runtime_) {
        if (!CheckCuda(cuCtxSetCurrent(runtime_->cudaContext), "cuCtxSetCurrent(existingContext)")) {
            runtime_.reset();
            return false;
        }
        if (!EnsureOutputBuffers(runtime_.get(), request.settings.width, request.settings.height)) {
            runtime_.reset();
            return false;
        }
        if (runtime_->sceneSignature != sceneSignature && !UploadSceneData(runtime_.get(), request.scene)) {
            runtime_.reset();
            return false;
        }
        return true;
    }

    auto runtime = std::make_unique<RuntimeState>();

    if (!CheckCuda(cuInit(0), "cuInit")) {
        return false;
    }
    if (!CheckCuda(cuDeviceGet(&runtime->device, 0), "cuDeviceGet")) {
        return false;
    }
    if (CheckCuda(cuDevicePrimaryCtxRetain(&runtime->cudaContext, runtime->device), "cuDevicePrimaryCtxRetain")) {
        runtime->usesPrimaryContext = true;
        if (!CheckCuda(cuCtxSetCurrent(runtime->cudaContext), "cuCtxSetCurrent(primaryContext)")) {
            return false;
        }
    } else {
        if (!CheckCuda(cuCtxCreate(&runtime->cudaContext, nullptr, 0, runtime->device), "cuCtxCreate")) {
            return false;
        }
    }
    if (!CheckCuda(cuStreamCreate(&runtime->stream, CU_STREAM_DEFAULT), "cuStreamCreate")) {
        return false;
    }
    if (!CheckOptix(optixInit(), "optixInit")) {
        return false;
    }

    OptixDeviceContextOptions contextOptions{};
    if (!CheckOptix(optixDeviceContextCreate(runtime->cudaContext, &contextOptions, &runtime->optixContext), "optixDeviceContextCreate")) {
        return false;
    }

    if (!BuildPipeline(runtime.get())) {
        return false;
    }
    if (!EnsureOutputBuffers(runtime.get(), request.settings.width, request.settings.height)) {
        return false;
    }
    if (!UploadSceneData(runtime.get(), request.scene)) {
        return false;
    }

    runtime_ = std::move(runtime);
    return true;
#endif
}

FrameBuffer OptixBackend::RenderSampleBatch(const RenderRequest& request) const {
    FrameBuffer frame;
    frame.Resize(request.settings.width, request.settings.height);
    frame.Clear();

    if (request.sampleCount == 0) {
        return frame;
    }

#if !(SHIRO_HAVE_CUDA && SHIRO_HAVE_OPTIX)
    return frame;
#else
    if (!EnsureRuntime(request)) {
        return frame;
    }

    std::scoped_lock lock(mutex_);
    if (!runtime_) {
        return frame;
    }
    if (!CheckCuda(cuCtxSetCurrent(runtime_->cudaContext), "cuCtxSetCurrent(render)")) {
        return frame;
    }

    LaunchParams params{};
    params.beauty = reinterpret_cast<OptixVec4f*>(runtime_->beautyBuffer);
    params.albedo = reinterpret_cast<OptixVec3f*>(runtime_->albedoBuffer);
    params.normal = reinterpret_cast<OptixVec3f*>(runtime_->normalBuffer);
    params.depth = reinterpret_cast<float*>(runtime_->depthBuffer);
    params.directionalLights = runtime_->directionalLights != 0
        ? reinterpret_cast<const OptixDirectionalLight*>(runtime_->directionalLights)
        : nullptr;
    params.domeLights = runtime_->domeLights != 0
        ? reinterpret_cast<const OptixDomeLight*>(runtime_->domeLights)
        : nullptr;
    params.directionalLightCount = runtime_->directionalLightCount;
    params.domeLightCount = runtime_->domeLightCount;
    params.domeLightSamples = request.settings.domeLightSamples;
    params.width = request.settings.width;
    params.height = request.settings.height;
    params.sampleStart = request.sampleStart;
    params.sampleCount = request.sampleCount;
    params.maxDepth = request.settings.maxDepth;
    params.diffuseDepth = request.settings.diffuseDepth;
    params.specularDepth = request.settings.specularDepth;
    params.backgroundVisible = request.settings.backgroundVisible ? 1u : 0u;
    params.traversable = static_cast<uint64_t>(runtime_->gasHandle);
    params.cameraPosition = ToOptixVec3f(request.camera.position);
    params.cameraForward = ToOptixVec3f(request.camera.forward);
    params.cameraRight = ToOptixVec3f(request.camera.right);
    params.cameraUp = ToOptixVec3f(request.camera.up);
    params.verticalFovDegrees = request.camera.verticalFovDegrees;
    params.aspectRatio = request.camera.aspectRatio;
    params.environmentTop = ToOptixVec3f(request.scene.environmentTop);
    params.environmentBottom = ToOptixVec3f(request.scene.environmentBottom);

    if (!CheckCuda(cuMemcpyHtoD(runtime_->launchParams, &params, sizeof(params)), "cuMemcpyHtoD(launchParams)")) {
        return frame;
    }
    if (!CheckOptix(optixLaunch(
            runtime_->pipeline,
            runtime_->stream,
            runtime_->launchParams,
            sizeof(params),
            &runtime_->sbt,
            request.settings.width,
            request.settings.height,
            1), "optixLaunch")) {
        return frame;
    }
    if (!CheckCuda(cuStreamSynchronize(runtime_->stream), "cuStreamSynchronize")) {
        return frame;
    }

    const size_t pixelCount = static_cast<size_t>(request.settings.width) * static_cast<size_t>(request.settings.height);
    std::vector<OptixVec4f> beauty(pixelCount);
    std::vector<OptixVec3f> albedo(pixelCount);
    std::vector<OptixVec3f> normal(pixelCount);
    std::vector<float> depth(pixelCount, std::numeric_limits<float>::infinity());

    if (!CheckCuda(cuMemcpyDtoH(beauty.data(), runtime_->beautyBuffer, beauty.size() * sizeof(OptixVec4f)), "cuMemcpyDtoH(beautyBuffer)")) {
        return frame;
    }
    if (!CheckCuda(cuMemcpyDtoH(albedo.data(), runtime_->albedoBuffer, albedo.size() * sizeof(OptixVec3f)), "cuMemcpyDtoH(albedoBuffer)")) {
        return frame;
    }
    if (!CheckCuda(cuMemcpyDtoH(normal.data(), runtime_->normalBuffer, normal.size() * sizeof(OptixVec3f)), "cuMemcpyDtoH(normalBuffer)")) {
        return frame;
    }
    if (!CheckCuda(cuMemcpyDtoH(depth.data(), runtime_->depthBuffer, depth.size() * sizeof(float)), "cuMemcpyDtoH(depthBuffer)")) {
        return frame;
    }

    for (uint32_t y = 0; y < request.settings.height; ++y) {
        for (uint32_t x = 0; x < request.settings.width; ++x) {
            const size_t index = static_cast<size_t>(y) * static_cast<size_t>(request.settings.width) + static_cast<size_t>(x);
            const OptixVec4f beautyValue = beauty[index];
            const float depthValue = depth[index] >= 1.0e15f
                ? std::numeric_limits<float>::infinity()
                : depth[index];
            frame.SetBeauty(x, y, {beautyValue.x, beautyValue.y, beautyValue.z, beautyValue.w});
            frame.SetAlbedo(x, y, {albedo[index].x, albedo[index].y, albedo[index].z});
            frame.SetNormal(x, y, {normal[index].x, normal[index].y, normal[index].z});
            frame.SetDepth(x, y, depthValue);
        }
    }

    return frame;
#endif
}

}  // namespace shiro::backend::optix
