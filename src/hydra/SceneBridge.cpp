#include "shiro/hydra/SceneBridge.h"

#if SHIRO_WITH_USD

#include <algorithm>
#include <cctype>
#include <cmath>
#include <initializer_list>
#include <optional>
#include <string>
#include <string_view>

#include <pxr/base/gf/matrix4d.h>
#include <pxr/base/gf/vec3d.h>
#include <pxr/base/gf/vec3f.h>
#include <pxr/base/gf/vec4f.h>
#include <pxr/base/gf/vec4d.h>
#include <pxr/base/vt/array.h>
#include <pxr/base/vt/value.h>
#include <pxr/imaging/hd/camera.h>
#include <pxr/imaging/hd/light.h>
#include <pxr/imaging/hd/material.h>
#include <pxr/imaging/hd/meshTopology.h>
#include <pxr/imaging/hd/renderPassState.h>
#include <pxr/imaging/hd/sceneDelegate.h>
#include <pxr/imaging/hd/tokens.h>
#include <pxr/usd/sdf/assetPath.h>

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

shiro::render::Vec3f ToVec3f(const GfVec4f& value) {
    return {value[0], value[1], value[2]};
}

shiro::render::Vec3f ToVec3f(const GfVec4d& value) {
    return {
        static_cast<float>(value[0]),
        static_cast<float>(value[1]),
        static_cast<float>(value[2]),
    };
}

shiro::render::Vec3f TransformPoint(const GfMatrix4d& matrix, const shiro::render::Vec3f& point) {
    return ToVec3f(matrix.Transform(GfVec3d(point.x, point.y, point.z)));
}

shiro::render::Vec3f TransformDirection(const GfMatrix4d& matrix, const shiro::render::Vec3f& direction) {
    return ToVec3f(matrix.TransformDir(GfVec3d(direction.x, direction.y, direction.z)));
}

std::optional<float> ToFloat(const VtValue& value) {
    if (value.IsHolding<float>()) {
        return value.UncheckedGet<float>();
    }
    if (value.IsHolding<double>()) {
        return static_cast<float>(value.UncheckedGet<double>());
    }
    if (value.IsHolding<int>()) {
        return static_cast<float>(value.UncheckedGet<int>());
    }
    return std::nullopt;
}

std::optional<shiro::render::Vec3f> ToColor(const VtValue& value) {
    if (value.IsHolding<GfVec3f>()) {
        return ToVec3f(value.UncheckedGet<GfVec3f>());
    }
    if (value.IsHolding<GfVec3d>()) {
        return ToVec3f(value.UncheckedGet<GfVec3d>());
    }
    if (value.IsHolding<GfVec4f>()) {
        return ToVec3f(value.UncheckedGet<GfVec4f>());
    }
    if (value.IsHolding<GfVec4d>()) {
        return ToVec3f(value.UncheckedGet<GfVec4d>());
    }
    return std::nullopt;
}

std::optional<TfToken> ToToken(const VtValue& value) {
    if (value.IsHolding<TfToken>()) {
        return value.UncheckedGet<TfToken>();
    }
    if (value.IsHolding<std::string>()) {
        return TfToken(value.UncheckedGet<std::string>());
    }
    return std::nullopt;
}

std::optional<bool> ToBool(const VtValue& value) {
    if (value.IsHolding<bool>()) {
        return value.UncheckedGet<bool>();
    }
    if (const auto scalar = ToFloat(value)) {
        return *scalar != 0.0f;
    }
    return std::nullopt;
}

std::optional<std::string> ToAssetPathString(const VtValue& value) {
    if (value.IsHolding<SdfAssetPath>()) {
        const SdfAssetPath& assetPath = value.UncheckedGet<SdfAssetPath>();
        return assetPath.GetResolvedPath().empty() ? assetPath.GetAssetPath() : assetPath.GetResolvedPath();
    }
    if (value.IsHolding<std::string>()) {
        return value.UncheckedGet<std::string>();
    }
    return std::nullopt;
}

VtValue GetPrimvarValue(HdSceneDelegate* sceneDelegate, const SdfPath& id, const TfToken& key) {
    VtIntArray indices;
    VtValue value = sceneDelegate->GetIndexedPrimvar(id, key, &indices);
    if (!value.IsEmpty()) {
        return value;
    }
    return sceneDelegate->Get(id, key);
}

bool HasRenderableFaces(const VtIntArray& faceVertexCounts) {
    for (const int faceVertexCount : faceVertexCounts) {
        if (faceVertexCount >= 3) {
            return true;
        }
    }
    return false;
}

bool HasRenderableTriangles(const shiro::render::TriangleMesh& mesh) {
    return mesh.positions.size() >= 3 && mesh.indices.size() >= 3;
}

void ComputeSmoothNormals(shiro::render::TriangleMesh* mesh) {
    if (!mesh || mesh->positions.empty() || mesh->indices.size() < 3) {
        return;
    }

    mesh->normals.assign(mesh->positions.size(), {0.0f, 0.0f, 0.0f});
    for (size_t index = 0; index + 2 < mesh->indices.size(); index += 3) {
        const uint32_t i0 = mesh->indices[index + 0u];
        const uint32_t i1 = mesh->indices[index + 1u];
        const uint32_t i2 = mesh->indices[index + 2u];
        if (i0 >= mesh->positions.size() || i1 >= mesh->positions.size() || i2 >= mesh->positions.size()) {
            continue;
        }

        const shiro::render::Vec3f edge01 = mesh->positions[i1] - mesh->positions[i0];
        const shiro::render::Vec3f edge02 = mesh->positions[i2] - mesh->positions[i0];
        const shiro::render::Vec3f faceNormal = shiro::render::Cross(edge01, edge02);
        if (shiro::render::Length(faceNormal) <= 1.0e-8f) {
            continue;
        }

        mesh->normals[i0] = mesh->normals[i0] + faceNormal;
        mesh->normals[i1] = mesh->normals[i1] + faceNormal;
        mesh->normals[i2] = mesh->normals[i2] + faceNormal;
    }

    for (shiro::render::Vec3f& normal : mesh->normals) {
        if (shiro::render::Length(normal) > 0.0f) {
            normal = shiro::render::Normalize(normal);
        } else {
            normal = {0.0f, 1.0f, 0.0f};
        }
    }
}

std::string ToLower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    return value;
}

bool ContainsCaseInsensitive(const TfToken& token, const char* needle) {
    return ToLower(token.GetString()).find(needle) != std::string::npos;
}

std::string NormalizeIdentifier(std::string value) {
    const size_t namespaceSeparator = value.rfind(':');
    if (namespaceSeparator != std::string::npos) {
        value = value.substr(namespaceSeparator + 1u);
    }

    std::string normalized;
    normalized.reserve(value.size());
    for (unsigned char character : value) {
        if (std::isalnum(character)) {
            normalized.push_back(static_cast<char>(std::tolower(character)));
        }
    }
    return normalized;
}

std::string NormalizeIdentifier(const TfToken& token) {
    return NormalizeIdentifier(token.GetString());
}

struct NumericValue {
    shiro::render::Vec3f color{};
    bool isColor = false;
};

shiro::render::Vec3f ScalarToColor(float value) {
    return {value, value, value};
}

shiro::render::Vec3f NumericColor(const NumericValue& value) {
    return value.isColor ? value.color : ScalarToColor(value.color.x);
}

float NumericScalar(const NumericValue& value) {
    return value.color.x;
}

std::optional<NumericValue> ToNumericValue(const VtValue& value) {
    if (const auto scalar = ToFloat(value)) {
        return NumericValue{ScalarToColor(*scalar), false};
    }
    if (const auto color = ToColor(value)) {
        return NumericValue{*color, true};
    }
    return std::nullopt;
}

VtValue ToVtValue(const NumericValue& value) {
    if (value.isColor) {
        return VtValue(GfVec3f(value.color.x, value.color.y, value.color.z));
    }
    return VtValue(value.color.x);
}

NumericValue MakeNumericResult(
    const NumericValue& lhs,
    const NumericValue& rhs,
    const shiro::render::Vec3f& color) {
    return NumericValue{color, lhs.isColor || rhs.isColor};
}

NumericValue AddNumeric(const NumericValue& lhs, const NumericValue& rhs) {
    return MakeNumericResult(lhs, rhs, NumericColor(lhs) + NumericColor(rhs));
}

NumericValue SubtractNumeric(const NumericValue& lhs, const NumericValue& rhs) {
    return MakeNumericResult(lhs, rhs, NumericColor(lhs) - NumericColor(rhs));
}

NumericValue MultiplyNumeric(const NumericValue& lhs, const NumericValue& rhs) {
    return MakeNumericResult(lhs, rhs, NumericColor(lhs) * NumericColor(rhs));
}

NumericValue DivideNumeric(const NumericValue& lhs, const NumericValue& rhs) {
    const shiro::render::Vec3f divisor = NumericColor(rhs);
    return MakeNumericResult(lhs, rhs, {
        NumericColor(lhs).x / (std::fabs(divisor.x) > 1.0e-6f ? divisor.x : 1.0f),
        NumericColor(lhs).y / (std::fabs(divisor.y) > 1.0e-6f ? divisor.y : 1.0f),
        NumericColor(lhs).z / (std::fabs(divisor.z) > 1.0e-6f ? divisor.z : 1.0f),
    });
}

NumericValue PowerNumeric(const NumericValue& lhs, const NumericValue& rhs) {
    const shiro::render::Vec3f exponent = NumericColor(rhs);
    return MakeNumericResult(lhs, rhs, {
        std::pow(std::max(0.0f, NumericColor(lhs).x), exponent.x),
        std::pow(std::max(0.0f, NumericColor(lhs).y), exponent.y),
        std::pow(std::max(0.0f, NumericColor(lhs).z), exponent.z),
    });
}

VtValue ExtractOutputValue(const VtValue& value, std::string_view outputName) {
    if (outputName.empty()
        || outputName == "out"
        || outputName == "value"
        || outputName == "result"
        || outputName == "outvalue"
        || outputName == "outcolor"
        || outputName == "rgb"
        || outputName == "xyz") {
        return value;
    }

    if (const auto color = ToColor(value)) {
        if (outputName == "r" || outputName == "x") {
            return VtValue(color->x);
        }
        if (outputName == "g" || outputName == "y") {
            return VtValue(color->y);
        }
        if (outputName == "b" || outputName == "z") {
            return VtValue(color->z);
        }
    }

    return value;
}

const VtValue* FindParameter(const HdMaterialNode2& node, std::string_view normalizedName) {
    for (const auto& [token, value] : node.parameters) {
        if (NormalizeIdentifier(token) == normalizedName) {
            return &value;
        }
    }
    return nullptr;
}

const std::vector<HdMaterialConnection2>* FindInputConnections(
    const HdMaterialNode2& node,
    std::string_view normalizedName) {
    for (const auto& [token, connections] : node.inputConnections) {
        if (NormalizeIdentifier(token) == normalizedName) {
            return &connections;
        }
    }
    return nullptr;
}

std::optional<HdMaterialNetwork2> ToMaterialNetwork(const VtValue& materialResource) {
    if (materialResource.IsHolding<HdMaterialNetwork2>()) {
        return materialResource.UncheckedGet<HdMaterialNetwork2>();
    }
    if (materialResource.IsHolding<HdMaterialNetworkMap>()) {
        return HdConvertToHdMaterialNetwork2(materialResource.UncheckedGet<HdMaterialNetworkMap>());
    }
    return std::nullopt;
}

const HdMaterialNode2* GetSurfaceNode(const HdMaterialNetwork2& network) {
    const auto terminalIt = network.terminals.find(HdMaterialTerminalTokens->surface);
    if (terminalIt == network.terminals.end()) {
        for (const auto& [terminalName, terminalConnection] : network.terminals) {
            if (NormalizeIdentifier(terminalName).find("surface") == std::string::npos) {
                continue;
            }

            const auto nodeIt = network.nodes.find(terminalConnection.upstreamNode);
            if (nodeIt != network.nodes.end()) {
                return &nodeIt->second;
            }
        }
        return nullptr;
    }

    const auto nodeIt = network.nodes.find(terminalIt->second.upstreamNode);
    if (nodeIt == network.nodes.end()) {
        return nullptr;
    }

    return &nodeIt->second;
}

const HdMaterialNode2* GetNode(const HdMaterialNetwork2& network, const SdfPath& path) {
    const auto nodeIt = network.nodes.find(path);
    return nodeIt != network.nodes.end() ? &nodeIt->second : nullptr;
}

std::optional<VtValue> ResolveNodeOutputValue(
    const HdMaterialNetwork2& network,
    const HdMaterialNode2& node,
    const TfToken& outputName,
    uint32_t depth);

std::optional<VtValue> ResolveInputValue(
    const HdMaterialNetwork2& network,
    const HdMaterialNode2& node,
    const std::string& normalizedInputName,
    uint32_t depth) {
    if (depth > 16u) {
        return std::nullopt;
    }

    if (const auto* connections = FindInputConnections(node, normalizedInputName)) {
        for (const HdMaterialConnection2& connection : *connections) {
            if (const HdMaterialNode2* upstreamNode = GetNode(network, connection.upstreamNode)) {
                if (const auto value = ResolveNodeOutputValue(network, *upstreamNode, connection.upstreamOutputName, depth + 1u)) {
                    return value;
                }
            }
        }
    }

    if (const VtValue* parameterValue = FindParameter(node, normalizedInputName)) {
        return *parameterValue;
    }

    return std::nullopt;
}

std::optional<VtValue> ResolveInputValue(
    const HdMaterialNetwork2& network,
    const HdMaterialNode2& node,
    std::initializer_list<const char*> inputNames,
    uint32_t depth) {
    for (const char* inputName : inputNames) {
        if (!inputName || *inputName == '\0') {
            continue;
        }
        const std::string normalizedName = NormalizeIdentifier(inputName);
        if (const auto value = ResolveInputValue(network, node, normalizedName, depth)) {
            return value;
        }
    }

    return std::nullopt;
}

std::optional<VtValue> ResolveNodeOutputValue(
    const HdMaterialNetwork2& network,
    const HdMaterialNode2& node,
    const TfToken& outputName,
    uint32_t depth) {
    if (depth > 16u) {
        return std::nullopt;
    }

    const std::string normalizedOutput = NormalizeIdentifier(outputName);
    const std::string normalizedNodeType = NormalizeIdentifier(node.nodeTypeId);

    const auto findParameter = [&](std::initializer_list<const char*> keys) -> std::optional<VtValue> {
        for (const char* key : keys) {
            if (!key || *key == '\0') {
                continue;
            }
            if (const VtValue* value = FindParameter(node, NormalizeIdentifier(key))) {
                return *value;
            }
        }
        return std::nullopt;
    };

    const auto resolveInput = [&](std::initializer_list<const char*> keys) -> std::optional<VtValue> {
        return ResolveInputValue(network, node, keys, depth);
    };

    const auto resolveBinaryNumeric = [&](
                                          std::initializer_list<const char*> lhsKeys,
                                          std::initializer_list<const char*> rhsKeys,
                                          const auto& combine) -> std::optional<VtValue> {
        const auto lhsValue = resolveInput(lhsKeys);
        const auto rhsValue = resolveInput(rhsKeys);
        if (!lhsValue || !rhsValue) {
            return std::nullopt;
        }
        const auto lhsNumeric = ToNumericValue(*lhsValue);
        const auto rhsNumeric = ToNumericValue(*rhsValue);
        if (!lhsNumeric || !rhsNumeric) {
            return std::nullopt;
        }
        return ToVtValue(combine(*lhsNumeric, *rhsNumeric));
    };

    const auto resolveUnaryNumeric = [&](
                                         std::initializer_list<const char*> valueKeys,
                                         const auto& combine) -> std::optional<VtValue> {
        const auto value = resolveInput(valueKeys);
        if (!value) {
            return std::nullopt;
        }
        const auto numeric = ToNumericValue(*value);
        if (!numeric) {
            return std::nullopt;
        }
        return ToVtValue(combine(*numeric));
    };

    if (!normalizedOutput.empty()) {
        if (const VtValue* direct = FindParameter(node, normalizedOutput)) {
            return *direct;
        }
    }

    auto finalizeValue = [&](const std::optional<VtValue>& value) -> std::optional<VtValue> {
        if (!value) {
            return std::nullopt;
        }
        return ExtractOutputValue(*value, normalizedOutput);
    };

    if (normalizedNodeType.find("constant") != std::string::npos
        || normalizedNodeType.find("uniform") != std::string::npos
        || normalizedNodeType == "value") {
        return finalizeValue(findParameter({"value", "default", "in", "input", "out", "outColor", "outValue", "rgb"}));
    }

    if (normalizedNodeType.find("image") != std::string::npos
        || normalizedNodeType.find("uvtexture") != std::string::npos
        || normalizedNodeType.find("primvarreader") != std::string::npos) {
        return finalizeValue(findParameter({"fallback", "default", "defaultColor", "fillColor", "value"}));
    }

    if (normalizedNodeType.find("multiply") != std::string::npos) {
        return finalizeValue(resolveBinaryNumeric({"in1", "fg", "a", "input1", "value1"}, {"in2", "bg", "b", "input2", "value2"}, MultiplyNumeric));
    }
    if (normalizedNodeType.find("divide") != std::string::npos) {
        return finalizeValue(resolveBinaryNumeric({"in1", "fg", "a", "input1", "value1"}, {"in2", "bg", "b", "input2", "value2"}, DivideNumeric));
    }
    if (normalizedNodeType.find("subtract") != std::string::npos || normalizedNodeType.find("minus") != std::string::npos) {
        return finalizeValue(resolveBinaryNumeric({"in1", "fg", "a", "input1", "value1"}, {"in2", "bg", "b", "input2", "value2"}, SubtractNumeric));
    }
    if (normalizedNodeType.find("add") != std::string::npos || normalizedNodeType.find("plus") != std::string::npos) {
        return finalizeValue(resolveBinaryNumeric({"in1", "fg", "a", "input1", "value1"}, {"in2", "bg", "b", "input2", "value2"}, AddNumeric));
    }
    if (normalizedNodeType.find("power") != std::string::npos) {
        return finalizeValue(resolveBinaryNumeric({"in1", "fg", "a", "input1", "value1"}, {"in2", "bg", "b", "input2", "value2"}, PowerNumeric));
    }
    if (normalizedNodeType.find("mix") != std::string::npos || normalizedNodeType.find("lerp") != std::string::npos) {
        const auto inputA = resolveInput({"in1", "fg", "bg", "a", "input1", "value1"});
        const auto inputB = resolveInput({"in2", "mix", "fg", "b", "input2", "value2"});
        const auto amount = resolveInput({"mix", "amount", "factor", "alpha", "mixval"});
        if (inputA && inputB && amount) {
            const auto numericA = ToNumericValue(*inputA);
            const auto numericB = ToNumericValue(*inputB);
            const auto numericAmount = ToNumericValue(*amount);
            if (numericA && numericB && numericAmount) {
                const float t = shiro::render::Clamp(NumericScalar(*numericAmount), 0.0f, 1.0f);
                return finalizeValue(std::optional<VtValue>(ToVtValue(MakeNumericResult(
                    *numericA,
                    *numericB,
                    shiro::render::Lerp(NumericColor(*numericA), NumericColor(*numericB), t)))));
            }
        }
    }
    if (normalizedNodeType.find("invert") != std::string::npos) {
        return finalizeValue(resolveUnaryNumeric({"in", "input", "value", "default"}, [](const NumericValue& input) {
            return NumericValue{
                {1.0f - NumericColor(input).x, 1.0f - NumericColor(input).y, 1.0f - NumericColor(input).z},
                input.isColor,
            };
        }));
    }
    if (normalizedNodeType.find("clamp") != std::string::npos) {
        const auto value = resolveInput({"in", "input", "value"});
        const auto low = resolveInput({"low", "min", "minimum"});
        const auto high = resolveInput({"high", "max", "maximum"});
        if (value && low && high) {
            const auto input = ToNumericValue(*value);
            const auto minValue = ToNumericValue(*low);
            const auto maxValue = ToNumericValue(*high);
            if (input && minValue && maxValue) {
                const shiro::render::Vec3f minColor = NumericColor(*minValue);
                const shiro::render::Vec3f maxColor = NumericColor(*maxValue);
                const shiro::render::Vec3f inputColor = NumericColor(*input);
                return finalizeValue(std::optional<VtValue>(ToVtValue(NumericValue{
                    {
                        shiro::render::Clamp(inputColor.x, minColor.x, maxColor.x),
                        shiro::render::Clamp(inputColor.y, minColor.y, maxColor.y),
                        shiro::render::Clamp(inputColor.z, minColor.z, maxColor.z),
                    },
                    input->isColor || minValue->isColor || maxValue->isColor,
                })));
            }
        }
    }
    if (normalizedNodeType.find("combine2") != std::string::npos
        || normalizedNodeType.find("combine3") != std::string::npos
        || normalizedNodeType.find("combine4") != std::string::npos) {
        const float x = ToFloat(resolveInput({"in1", "x", "r"}).value_or(VtValue(0.0f))).value_or(0.0f);
        const float y = ToFloat(resolveInput({"in2", "y", "g"}).value_or(VtValue(0.0f))).value_or(0.0f);
        const float z = ToFloat(resolveInput({"in3", "z", "b"}).value_or(VtValue(0.0f))).value_or(0.0f);
        return finalizeValue(std::optional<VtValue>(VtValue(GfVec3f(x, y, z))));
    }
    if (normalizedNodeType.find("separate") != std::string::npos
        || normalizedNodeType.find("extract") != std::string::npos
        || normalizedNodeType.find("swizzle") != std::string::npos) {
        const auto input = resolveInput({"in", "input", "value"});
        if (input) {
            return finalizeValue(std::optional<VtValue>(*input));
        }
    }

    return finalizeValue(findParameter({"value", "out", "outColor", "outValue", "rgb", "default"}));
}

float ResolveFloatParameter(
    const HdMaterialNetwork2& network,
    const HdMaterialNode2& node,
    std::initializer_list<const char*> keys,
    float fallback) {
    if (const auto value = ResolveInputValue(network, node, keys, 0u)) {
        if (const auto scalar = ToFloat(*value)) {
            return *scalar;
        }
        if (const auto color = ToColor(*value)) {
            return shiro::render::MaxComponent(*color);
        }
    }

    return fallback;
}

shiro::render::Vec3f ResolveColorParameter(
    const HdMaterialNetwork2& network,
    const HdMaterialNode2& node,
    std::initializer_list<const char*> keys,
    const shiro::render::Vec3f& fallback) {
    if (const auto value = ResolveInputValue(network, node, keys, 0u)) {
        if (const auto color = ToColor(*value)) {
            return *color;
        }
        if (const auto scalar = ToFloat(*value)) {
            return {*scalar, *scalar, *scalar};
        }
    }

    return fallback;
}

std::optional<shiro::render::Vec3f> ResolveOptionalVectorParameter(
    const HdMaterialNetwork2& network,
    const HdMaterialNode2& node,
    std::initializer_list<const char*> keys) {
    if (const auto value = ResolveInputValue(network, node, keys, 0u)) {
        if (const auto color = ToColor(*value)) {
            return shiro::render::Normalize(*color);
        }
    }

    return std::nullopt;
}

bool ResolveBoolParameter(
    const HdMaterialNetwork2& network,
    const HdMaterialNode2& node,
    std::initializer_list<const char*> keys,
    bool fallback) {
    if (const auto value = ResolveInputValue(network, node, keys, 0u)) {
        if (const auto boolean = ToBool(*value)) {
            return *boolean;
        }
    }

    return fallback;
}

shiro::render::PbrMaterial ParseUsdPreviewSurface(const HdMaterialNetwork2& network, const HdMaterialNode2& node) {
    shiro::render::PbrMaterial material;
    material.baseColor = ResolveColorParameter(network, node, {"diffuseColor", "diffuse_color", "baseColor", "base_color"}, material.baseColor);
    material.roughness = ResolveFloatParameter(network, node, {"roughness", "specular_roughness"}, material.roughness);
    material.metallic = ResolveFloatParameter(network, node, {"metallic", "metalness"}, material.metallic);
    material.ior = ResolveFloatParameter(network, node, {"ior", "specular_IOR", "specular_ior"}, material.ior);
    material.opacity = ResolveFloatParameter(network, node, {"opacity"}, material.opacity);
    material.thinWalled = material.opacity < 1.0f;
    material.specularWeight = 1.0f;
    material.specularColor = {1.0f, 1.0f, 1.0f};
    material.emissionColor = ResolveColorParameter(network, node, {"emissiveColor", "emissionColor", "emission_color"}, material.emissionColor);
    material.emissionStrength =
        shiro::render::MaxComponent(material.emissionColor) > 0.0f ? 1.0f : 0.0f;
    return material;
}

shiro::render::PbrMaterial ParseStandardSurface(const HdMaterialNetwork2& network, const HdMaterialNode2& node) {
    shiro::render::PbrMaterial material;
    material.baseWeight = ResolveFloatParameter(network, node, {"base", "base_weight"}, material.baseWeight);
    material.baseColor = ResolveColorParameter(network, node, {"base_color", "baseColor", "diffuseColor", "diffuse_color"}, material.baseColor);
    material.diffuseRoughness = ResolveFloatParameter(network, node, {"diffuse_roughness", "diffuseRoughness"}, material.diffuseRoughness);
    material.specularWeight = ResolveFloatParameter(network, node, {"specular", "specular_weight"}, material.specularWeight);
    material.specularColor = ResolveColorParameter(network, node, {"specular_color", "specularColor"}, material.specularColor);
    material.roughness = ResolveFloatParameter(network, node, {"specular_roughness", "roughness", "base_roughness"}, material.roughness);
    material.metallic = ResolveFloatParameter(network, node, {"metalness", "metallic", "base_metalness"}, material.metallic);
    material.ior = ResolveFloatParameter(network, node, {"specular_IOR", "specular_ior", "ior"}, material.ior);
    material.specularAnisotropy =
        ResolveFloatParameter(network, node, {"specular_anisotropy", "specularAnisotropy"}, material.specularAnisotropy);
    material.specularRotation =
        ResolveFloatParameter(network, node, {"specular_rotation", "specularRotation"}, material.specularRotation);
    material.transmission = ResolveFloatParameter(network, node, {"transmission", "transmission_weight", "specular_transmission"}, material.transmission);
    if (material.transmission <= 0.0f) {
        material.transmission = ResolveFloatParameter(network, node, {"transmission_weight", "transmission", "specular_transmission"}, material.transmission);
    }
    material.transmissionColor = ResolveColorParameter(network, node, {"transmission_color", "transmissionColor"}, material.transmissionColor);
    material.transmissionDepth =
        ResolveFloatParameter(network, node, {"transmission_depth", "transmissionDepth"}, material.transmissionDepth);
    material.transmissionScatter = ResolveColorParameter(
        network,
        node,
        {"transmission_scatter", "transmissionScatter"},
        material.transmissionScatter);
    material.transmissionScatterAnisotropy = ResolveFloatParameter(
        network,
        node,
        {"transmission_scatter_anisotropy", "transmissionScatterAnisotropy"},
        material.transmissionScatterAnisotropy);
    material.transmissionDispersion = ResolveFloatParameter(
        network,
        node,
        {"transmission_dispersion", "transmissionDispersion"},
        material.transmissionDispersion);
    material.transmissionExtraRoughness = ResolveFloatParameter(
        network,
        node,
        {"transmission_extra_roughness", "transmissionExtraRoughness"},
        material.transmissionExtraRoughness);
    material.thinWalled = ResolveBoolParameter(network, node, {"thin_walled", "thinWalled"}, material.thinWalled);
    material.coatWeight = ResolveFloatParameter(network, node, {"coat", "coat_weight", "clearcoat"}, material.coatWeight);
    if (material.coatWeight <= 0.0f) {
        material.coatWeight = ResolveFloatParameter(network, node, {"coat_weight", "coat", "clearcoat"}, material.coatWeight);
    }
    material.coatColor = ResolveColorParameter(network, node, {"coat_color", "coatColor", "clearcoat_color"}, material.coatColor);
    material.coatRoughness = ResolveFloatParameter(network, node, {"coat_roughness", "coatRoughness", "clearcoat_roughness"}, material.coatRoughness);
    material.coatIor = ResolveFloatParameter(network, node, {"coat_IOR", "coat_ior", "coatIor"}, material.coatIor);
    material.coatAnisotropy =
        ResolveFloatParameter(network, node, {"coat_anisotropy", "coatAnisotropy"}, material.coatAnisotropy);
    material.coatRotation =
        ResolveFloatParameter(network, node, {"coat_rotation", "coatRotation"}, material.coatRotation);
    material.coatAffectColor = ResolveFloatParameter(
        network,
        node,
        {"coat_affect_color", "coatAffectColor"},
        material.coatAffectColor);
    material.coatAffectRoughness = ResolveFloatParameter(
        network,
        node,
        {"coat_affect_roughness", "coatAffectRoughness"},
        material.coatAffectRoughness);
    material.subsurface = ResolveFloatParameter(network, node, {"subsurface", "subsurface_weight"}, material.subsurface);
    material.subsurfaceColor =
        ResolveColorParameter(network, node, {"subsurface_color", "subsurfaceColor"}, material.subsurfaceColor);
    material.subsurfaceRadius =
        ResolveColorParameter(network, node, {"subsurface_radius", "subsurfaceRadius"}, material.subsurfaceRadius);
    material.subsurfaceScale =
        ResolveFloatParameter(network, node, {"subsurface_scale", "subsurfaceScale"}, material.subsurfaceScale);
    material.subsurfaceAnisotropy = ResolveFloatParameter(
        network,
        node,
        {"subsurface_anisotropy", "subsurfaceAnisotropy"},
        material.subsurfaceAnisotropy);
    material.sheen = ResolveFloatParameter(network, node, {"sheen", "sheen_weight"}, material.sheen);
    material.sheenColor = ResolveColorParameter(network, node, {"sheen_color", "sheenColor"}, material.sheenColor);
    material.sheenRoughness =
        ResolveFloatParameter(network, node, {"sheen_roughness", "sheenRoughness"}, material.sheenRoughness);
    material.thinFilmThickness = ResolveFloatParameter(
        network,
        node,
        {"thin_film_thickness", "thinFilmThickness"},
        material.thinFilmThickness);
    material.thinFilmIor = ResolveFloatParameter(
        network,
        node,
        {"thin_film_IOR", "thin_film_ior", "thinFilmIor"},
        material.thinFilmIor);
    material.emissionColor = ResolveColorParameter(network, node, {"emission_color", "emissionColor", "emissiveColor"}, material.emissionColor);
    material.emissionStrength = ResolveFloatParameter(network, node, {"emission", "emission_strength", "emission_luminance"}, 0.0f);
    material.opacity = ResolveFloatParameter(network, node, {"opacity", "geometry_opacity"}, material.opacity);
    if (const auto normal = ResolveOptionalVectorParameter(network, node, {"normal"})) {
        material.normalOverride = *normal;
        material.hasNormalOverride = true;
    }
    if (const auto coatNormal = ResolveOptionalVectorParameter(network, node, {"coat_normal", "coatNormal"})) {
        material.coatNormalOverride = *coatNormal;
        material.hasCoatNormalOverride = true;
    }
    if (const auto tangent = ResolveOptionalVectorParameter(network, node, {"tangent"})) {
        material.tangentOverride = *tangent;
        material.hasTangentOverride = true;
    }
    if (material.emissionStrength <= 0.0f && shiro::render::MaxComponent(material.emissionColor) > 0.0f) {
        material.emissionStrength = 1.0f;
    }
    return material;
}

shiro::render::PbrMaterial ParseOpenPbrSurface(const HdMaterialNetwork2& network, const HdMaterialNode2& node) {
    shiro::render::PbrMaterial material;
    material.baseWeight = ResolveFloatParameter(network, node, {"base_weight", "base"}, material.baseWeight);
    material.baseColor = ResolveColorParameter(network, node, {"base_color", "baseColor", "diffuseColor"}, material.baseColor);
    material.diffuseRoughness =
        ResolveFloatParameter(network, node, {"base_diffuse_roughness", "diffuse_roughness"}, material.diffuseRoughness);
    material.metallic = ResolveFloatParameter(network, node, {"base_metalness", "metalness", "metallic"}, material.metallic);
    material.specularWeight = ResolveFloatParameter(network, node, {"specular_weight", "specular"}, material.specularWeight);
    material.specularColor = ResolveColorParameter(network, node, {"specular_color", "specularColor"}, material.specularColor);
    material.roughness = ResolveFloatParameter(network, node, {"specular_roughness", "roughness", "base_roughness"}, material.roughness);
    material.ior = ResolveFloatParameter(network, node, {"specular_ior", "specular_IOR", "ior"}, material.ior);
    material.specularAnisotropy = ResolveFloatParameter(
        network,
        node,
        {"specular_anisotropy", "specularAnisotropy"},
        material.specularAnisotropy);
    material.specularRotation = ResolveFloatParameter(
        network,
        node,
        {"specular_rotation", "specularRotation"},
        material.specularRotation);
    material.transmission = ResolveFloatParameter(network, node, {"transmission_weight", "transmission", "specular_transmission"}, material.transmission);
    if (material.transmission <= 0.0f) {
        material.transmission = ResolveFloatParameter(network, node, {"transmission", "transmission_weight", "specular_transmission"}, material.transmission);
    }
    material.transmissionColor = ResolveColorParameter(network, node, {"transmission_color", "transmissionColor"}, material.transmissionColor);
    material.transmissionDepth =
        ResolveFloatParameter(network, node, {"transmission_depth", "transmissionDepth"}, material.transmissionDepth);
    material.transmissionScatter = ResolveColorParameter(
        network,
        node,
        {"transmission_scatter", "transmissionScatter"},
        material.transmissionScatter);
    material.transmissionScatterAnisotropy = ResolveFloatParameter(
        network,
        node,
        {"transmission_scatter_anisotropy", "transmissionScatterAnisotropy"},
        material.transmissionScatterAnisotropy);
    material.transmissionDispersion = ResolveFloatParameter(
        network,
        node,
        {"transmission_dispersion", "transmissionDispersion"},
        material.transmissionDispersion);
    material.transmissionExtraRoughness = ResolveFloatParameter(
        network,
        node,
        {"transmission_extra_roughness", "transmissionExtraRoughness"},
        material.transmissionExtraRoughness);
    material.thinWalled = ResolveBoolParameter(network, node, {"thin_walled", "thinWalled"}, material.thinWalled);
    material.coatWeight = ResolveFloatParameter(network, node, {"coat_weight", "coat", "clearcoat"}, material.coatWeight);
    material.coatColor = ResolveColorParameter(network, node, {"coat_color", "coatColor", "clearcoat_color"}, material.coatColor);
    material.coatRoughness = ResolveFloatParameter(network, node, {"coat_roughness", "coatRoughness", "clearcoat_roughness"}, material.coatRoughness);
    material.coatIor = ResolveFloatParameter(network, node, {"coat_ior", "coat_IOR", "coatIor"}, material.coatIor);
    material.coatAnisotropy =
        ResolveFloatParameter(network, node, {"coat_anisotropy", "coatAnisotropy"}, material.coatAnisotropy);
    material.coatRotation =
        ResolveFloatParameter(network, node, {"coat_rotation", "coatRotation"}, material.coatRotation);
    material.coatAffectColor = ResolveFloatParameter(
        network,
        node,
        {"coat_affect_color", "coatAffectColor"},
        material.coatAffectColor);
    material.coatAffectRoughness = ResolveFloatParameter(
        network,
        node,
        {"coat_affect_roughness", "coatAffectRoughness"},
        material.coatAffectRoughness);
    material.subsurface = ResolveFloatParameter(network, node, {"subsurface_weight", "subsurface"}, material.subsurface);
    material.subsurfaceColor =
        ResolveColorParameter(network, node, {"subsurface_color", "subsurfaceColor"}, material.subsurfaceColor);
    material.subsurfaceRadius =
        ResolveColorParameter(network, node, {"subsurface_radius", "subsurfaceRadius"}, material.subsurfaceRadius);
    material.subsurfaceScale =
        ResolveFloatParameter(network, node, {"subsurface_scale", "subsurfaceScale"}, material.subsurfaceScale);
    material.subsurfaceAnisotropy = ResolveFloatParameter(
        network,
        node,
        {"subsurface_anisotropy", "subsurfaceAnisotropy"},
        material.subsurfaceAnisotropy);
    material.sheen = ResolveFloatParameter(network, node, {"fuzz_weight", "sheen_weight", "sheen"}, material.sheen);
    material.sheenColor =
        ResolveColorParameter(network, node, {"fuzz_color", "sheen_color", "sheenColor"}, material.sheenColor);
    material.sheenRoughness =
        ResolveFloatParameter(network, node, {"fuzz_roughness", "sheen_roughness", "sheenRoughness"}, material.sheenRoughness);
    material.thinFilmThickness = ResolveFloatParameter(
        network,
        node,
        {"thin_film_thickness", "thinFilmThickness"},
        material.thinFilmThickness);
    material.thinFilmIor = ResolveFloatParameter(
        network,
        node,
        {"thin_film_ior", "thin_film_IOR", "thinFilmIor"},
        material.thinFilmIor);
    material.emissionColor = ResolveColorParameter(network, node, {"emission_color", "emissionColor", "emissiveColor"}, material.emissionColor);
    material.emissionStrength = ResolveFloatParameter(network, node, {"emission_luminance", "emission", "emission_strength"}, 0.0f);
    material.opacity = ResolveFloatParameter(network, node, {"geometry_opacity", "opacity"}, material.opacity);
    if (const auto normal = ResolveOptionalVectorParameter(network, node, {"normal"})) {
        material.normalOverride = *normal;
        material.hasNormalOverride = true;
    }
    if (const auto coatNormal = ResolveOptionalVectorParameter(network, node, {"coat_normal", "coatNormal"})) {
        material.coatNormalOverride = *coatNormal;
        material.hasCoatNormalOverride = true;
    }
    if (const auto tangent = ResolveOptionalVectorParameter(network, node, {"tangent"})) {
        material.tangentOverride = *tangent;
        material.hasTangentOverride = true;
    }
    if (material.emissionStrength <= 0.0f) {
        material.emissionStrength = ResolveFloatParameter(network, node, {"emission", "emission_luminance", "emission_strength"}, 0.0f);
    }
    if (material.emissionStrength <= 0.0f && shiro::render::MaxComponent(material.emissionColor) > 0.0f) {
        material.emissionStrength = 1.0f;
    }
    return material;
}

std::optional<shiro::render::PbrMaterial> ParseMaterialNode(const HdMaterialNetwork2& network, const HdMaterialNode2& node) {
    if (ContainsCaseInsensitive(node.nodeTypeId, "usdpreviewsurface")) {
        return ParseUsdPreviewSurface(network, node);
    }

    if (ContainsCaseInsensitive(node.nodeTypeId, "open_pbr_surface")
        || ContainsCaseInsensitive(node.nodeTypeId, "openpbr_surface")
        || ContainsCaseInsensitive(node.nodeTypeId, "openpbr")) {
        return ParseOpenPbrSurface(network, node);
    }

    if (ContainsCaseInsensitive(node.nodeTypeId, "standard_surface")) {
        return ParseStandardSurface(network, node);
    }

    return std::nullopt;
}

shiro::render::Vec3f ComputeLightRadiance(HdSceneDelegate* sceneDelegate, const SdfPath& id) {
    shiro::render::Vec3f color{1.0f, 1.0f, 1.0f};
    const float intensity = ToFloat(sceneDelegate->GetLightParamValue(id, HdLightTokens->intensity)).value_or(1.0f);
    const float exposure = ToFloat(sceneDelegate->GetLightParamValue(id, HdLightTokens->exposure)).value_or(0.0f);

    if (const auto authoredColor = ToColor(sceneDelegate->GetLightParamValue(id, HdLightTokens->color))) {
        color = *authoredColor;
    }

    return color * intensity * std::pow(2.0f, exposure);
}

shiro::render::EnvironmentMapLayout ParseEnvironmentMapLayout(const VtValue& value) {
    const auto tokenValue = ToToken(value);
    if (!tokenValue) {
        return shiro::render::EnvironmentMapLayout::Automatic;
    }

    const std::string token = ToLower(tokenValue->GetString());
    if (token == "angular") {
        return shiro::render::EnvironmentMapLayout::Angular;
    }
    if (token == "latlong" || token == "automatic" || token == "mirroredball" || token == "cubemapverticalcross") {
        return shiro::render::EnvironmentMapLayout::LatLong;
    }
    return shiro::render::EnvironmentMapLayout::Automatic;
}

shiro::render::PbrMaterial BuildFallbackMaterial(HdSceneDelegate* sceneDelegate, const SdfPath& id) {
    shiro::render::PbrMaterial material;

    const VtValue colorValue = GetPrimvarValue(sceneDelegate, id, HdTokens->displayColor);
    if (colorValue.IsHolding<VtVec3fArray>() && !colorValue.UncheckedGet<VtVec3fArray>().empty()) {
        material.baseColor = ToVec3f(colorValue.UncheckedGet<VtVec3fArray>()[0]);
    }

    const VtValue metallicValue = sceneDelegate->Get(id, TfToken("metallic"));
    if (const auto metallic = ToFloat(metallicValue)) {
        material.metallic = *metallic;
    }

    const VtValue roughnessValue = sceneDelegate->Get(id, TfToken("roughness"));
    if (const auto roughness = ToFloat(roughnessValue)) {
        material.roughness = *roughness;
    }

    const VtValue emissionValue = sceneDelegate->Get(id, TfToken("emissiveColor"));
    if (const auto emissionColor = ToColor(emissionValue)) {
        material.emissionColor = *emissionColor;
        material.emissionStrength = shiro::render::MaxComponent(*emissionColor) > 0.0f ? 1.0f : 0.0f;
    }

    return material;
}

}  // namespace

std::optional<HdShiroMeshPayload> HdShiroSceneBridge::ExtractMesh(
    HdSceneDelegate* sceneDelegate,
    const SdfPath& id,
    uint32_t maxSubdivLevel) {
    (void)maxSubdivLevel;
    if (!sceneDelegate || !sceneDelegate->GetVisible(id)) {
        return std::nullopt;
    }

    const VtValue pointsValue = GetPrimvarValue(sceneDelegate, id, HdTokens->points);
    if (!pointsValue.IsHolding<VtVec3fArray>()) {
        return std::nullopt;
    }

    const GfMatrix4d transform = sceneDelegate->GetTransform(id);
    const VtVec3fArray& points = pointsValue.UncheckedGet<VtVec3fArray>();
    const HdMeshTopology topology = sceneDelegate->GetMeshTopology(id);
    const VtIntArray& faceVertexCounts = topology.GetFaceVertexCounts();
    const VtIntArray& faceVertexIndices = topology.GetFaceVertexIndices();
    if (points.empty() || faceVertexIndices.empty() || !HasRenderableFaces(faceVertexCounts)) {
        return std::nullopt;
    }

    HdShiroMeshPayload payload;
    payload.mesh.positions.reserve(points.size());

    for (const GfVec3f& point : points) {
        payload.mesh.positions.push_back(TransformPoint(transform, ToVec3f(point)));
    }

    const VtValue normalsValue = GetPrimvarValue(sceneDelegate, id, HdTokens->normals);
    if (normalsValue.IsHolding<VtVec3fArray>()) {
        const VtVec3fArray& normals = normalsValue.UncheckedGet<VtVec3fArray>();
        if (normals.size() == points.size()) {
            payload.mesh.normals.reserve(normals.size());
            for (const GfVec3f& normal : normals) {
                payload.mesh.normals.push_back(
                    shiro::render::Normalize(TransformDirection(transform, ToVec3f(normal))));
            }
        }
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

    if (!HasRenderableTriangles(payload.mesh)) {
        return std::nullopt;
    }

    if (payload.mesh.normals.size() != payload.mesh.positions.size()) {
        ComputeSmoothNormals(&payload.mesh);
    }

    payload.fallbackMaterial = BuildFallbackMaterial(sceneDelegate, id);
    return payload;
}

std::optional<shiro::render::PbrMaterial> HdShiroSceneBridge::ExtractMaterial(const VtValue& materialResource) {
    const auto network = ToMaterialNetwork(materialResource);
    if (!network) {
        return std::nullopt;
    }

    const HdMaterialNode2* surfaceNode = GetSurfaceNode(*network);
    if (!surfaceNode) {
        return std::nullopt;
    }

    return ParseMaterialNode(*network, *surfaceNode);
}

std::optional<shiro::render::DomeLight> HdShiroSceneBridge::ExtractDomeLight(
    HdSceneDelegate* sceneDelegate,
    const SdfPath& id) {
    if (!sceneDelegate || !sceneDelegate->GetVisible(id)) {
        return std::nullopt;
    }

    shiro::render::DomeLight light;
    light.radiance = ComputeLightRadiance(sceneDelegate, id);
    light.layout = ParseEnvironmentMapLayout(sceneDelegate->GetLightParamValue(id, HdLightTokens->textureFormat));
    if (const auto textureFile = ToAssetPathString(sceneDelegate->GetLightParamValue(id, HdLightTokens->textureFile))) {
        light.textureFile = *textureFile;
    }

    const GfMatrix4d transform = sceneDelegate->GetTransform(id);
    light.right = shiro::render::Normalize(TransformDirection(transform, {1.0f, 0.0f, 0.0f}));
    light.up = shiro::render::Normalize(TransformDirection(transform, {0.0f, 1.0f, 0.0f}));
    light.forward = shiro::render::Normalize(TransformDirection(transform, {0.0f, 0.0f, 1.0f}));
    if (shiro::render::Length(light.right) <= 0.0f) {
        light.right = {1.0f, 0.0f, 0.0f};
    }
    if (shiro::render::Length(light.up) <= 0.0f) {
        light.up = {0.0f, 1.0f, 0.0f};
    }
    if (shiro::render::Length(light.forward) <= 0.0f) {
        light.forward = {0.0f, 0.0f, 1.0f};
    }
    return light;
}

std::optional<shiro::render::DirectionalLight> HdShiroSceneBridge::ExtractDistantLight(
    HdSceneDelegate* sceneDelegate,
    const SdfPath& id) {
    if (!sceneDelegate || !sceneDelegate->GetVisible(id)) {
        return std::nullopt;
    }

    shiro::render::DirectionalLight light;
    light.direction = shiro::render::Normalize(ToVec3f(sceneDelegate->GetTransform(id).TransformDir(GfVec3d(0.0, 0.0, -1.0))));
    if (shiro::render::Length(light.direction) <= 0.0f) {
        light.direction = {0.0f, -1.0f, -1.0f};
    }
    light.radiance = ComputeLightRadiance(sceneDelegate, id);
    return light;
}

shiro::render::Camera HdShiroSceneBridge::BuildCamera(
    const HdRenderPassStateSharedPtr& renderPassState,
    uint32_t imageWidth,
    uint32_t imageHeight) {
    shiro::render::Camera camera;

    if (imageWidth > 0 && imageHeight > 0) {
        camera.aspectRatio = static_cast<float>(imageWidth) / static_cast<float>(imageHeight);
    } else {
        const GfVec4d viewport = renderPassState->GetViewport();
        camera.aspectRatio = viewport[3] > 0.0 ? static_cast<float>(viewport[2] / viewport[3]) : 1.0f;
    }

    const HdCamera* hdCamera = renderPassState->GetCamera();
    if (hdCamera) {
        const GfMatrix4d transform = hdCamera->GetTransform();
        camera.position = ToVec3f(transform.ExtractTranslation());
        camera.forward = shiro::render::Normalize(TransformDirection(transform, {0.0f, 0.0f, -1.0f}));
        camera.right = shiro::render::Normalize(TransformDirection(transform, {1.0f, 0.0f, 0.0f}));
        camera.up = shiro::render::Normalize(TransformDirection(transform, {0.0f, 1.0f, 0.0f}));

        const float focalLength = hdCamera->GetFocalLength();
        const float verticalAperture = hdCamera->GetVerticalAperture();
        if (hdCamera->GetProjection() == HdCamera::Perspective
            && focalLength > 1.0e-6f
            && verticalAperture > 1.0e-6f) {
            camera.verticalFovDegrees =
                static_cast<float>(2.0 * std::atan(0.5 * verticalAperture / focalLength) * 180.0 / 3.14159265358979323846);
        }

        if (shiro::render::Length(camera.forward) > 0.0f
            && shiro::render::Length(camera.right) > 0.0f
            && shiro::render::Length(camera.up) > 0.0f) {
            return camera;
        }
    }

    const GfMatrix4d viewToWorld = renderPassState->GetWorldToViewMatrix().GetInverse();
    camera.position = ToVec3f(viewToWorld.ExtractTranslation());
    camera.forward = shiro::render::Normalize(ToVec3f(viewToWorld.TransformDir(GfVec3d(0.0, 0.0, -1.0))));
    camera.right = shiro::render::Normalize(ToVec3f(viewToWorld.TransformDir(GfVec3d(1.0, 0.0, 0.0))));
    camera.up = shiro::render::Normalize(ToVec3f(viewToWorld.TransformDir(GfVec3d(0.0, 1.0, 0.0))));

    const GfMatrix4d projection = renderPassState->GetProjectionMatrix();
    if (std::fabs(projection[1][1]) > 1.0e-6) {
        camera.verticalFovDegrees =
            static_cast<float>(2.0 * std::atan(1.0 / projection[1][1]) * 180.0 / 3.14159265358979323846);
    }

    return camera;
}

PXR_NAMESPACE_CLOSE_SCOPE

#endif
