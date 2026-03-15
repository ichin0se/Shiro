#include "shiro/hydra/RenderDelegate.h"

#if SHIRO_WITH_USD

#include <pxr/base/gf/vec3f.h>
#include <pxr/base/gf/vec4f.h>
#include <pxr/base/vt/value.h>
#include <pxr/imaging/hd/aov.h>
#include <pxr/imaging/hd/instancer.h>
#include <pxr/imaging/hd/tokens.h>

#include "shiro/hydra/Camera.h"
#include "shiro/hydra/Material.h"
#include "shiro/hydra/Mesh.h"
#include "shiro/hydra/RenderBuffer.h"
#include "shiro/hydra/RenderParam.h"
#include "shiro/hydra/RenderPass.h"
#include "shiro/hydra/Tokens.h"

PXR_NAMESPACE_OPEN_SCOPE

namespace {

const TfTokenVector& SupportedRprimTypes() {
    static const TfTokenVector tokens = {HdPrimTypeTokens->mesh};
    return tokens;
}

const TfTokenVector& SupportedSprimTypes() {
    static const TfTokenVector tokens = {HdPrimTypeTokens->camera, HdPrimTypeTokens->material};
    return tokens;
}

const TfTokenVector& SupportedBprimTypes() {
    static const TfTokenVector tokens = {HdPrimTypeTokens->renderBuffer};
    return tokens;
}

TfTokenVector MaterialRenderContexts() {
    static const TfTokenVector tokens = {HdShiroTokens->shiro};
    return tokens;
}

}  // namespace

HdShiroRenderDelegate::HdShiroRenderDelegate()
    : renderParam_(std::make_unique<HdShiroRenderParam>()),
      resourceRegistry_(std::make_shared<HdResourceRegistry>()) {
}

HdShiroRenderDelegate::HdShiroRenderDelegate(const HdRenderSettingsMap& settingsMap)
    : HdShiroRenderDelegate() {
    ApplySettingsMap(settingsMap);
}

HdShiroRenderDelegate::~HdShiroRenderDelegate() = default;

const TfTokenVector& HdShiroRenderDelegate::GetSupportedRprimTypes() const {
    return SupportedRprimTypes();
}

const TfTokenVector& HdShiroRenderDelegate::GetSupportedSprimTypes() const {
    return SupportedSprimTypes();
}

const TfTokenVector& HdShiroRenderDelegate::GetSupportedBprimTypes() const {
    return SupportedBprimTypes();
}

HdRenderParam* HdShiroRenderDelegate::GetRenderParam() const {
    return renderParam_.get();
}

HdResourceRegistrySharedPtr HdShiroRenderDelegate::GetResourceRegistry() const {
    return resourceRegistry_;
}

HdRenderPassSharedPtr HdShiroRenderDelegate::CreateRenderPass(HdRenderIndex* renderIndex, const HdRprimCollection& collection) {
    return HdRenderPassSharedPtr(new HdShiroRenderPass(renderIndex, collection, renderParam_.get()));
}

HdInstancer* HdShiroRenderDelegate::CreateInstancer(HdSceneDelegate* sceneDelegate, const SdfPath& id) {
    return new HdInstancer(sceneDelegate, id);
}

void HdShiroRenderDelegate::DestroyInstancer(HdInstancer* instancer) {
    delete instancer;
}

HdRprim* HdShiroRenderDelegate::CreateRprim(const TfToken& typeId, const SdfPath& rprimId) {
    if (typeId == HdPrimTypeTokens->mesh) {
        return new HdShiroMesh(rprimId);
    }

    return nullptr;
}

HdSprim* HdShiroRenderDelegate::CreateSprim(const TfToken& typeId, const SdfPath& sprimId) {
    if (typeId == HdPrimTypeTokens->camera) {
        return new HdShiroCamera(sprimId);
    }

    if (typeId == HdPrimTypeTokens->material) {
        return new HdShiroMaterial(sprimId);
    }

    return nullptr;
}

HdSprim* HdShiroRenderDelegate::CreateFallbackSprim(const TfToken& typeId) {
    return CreateSprim(typeId, SdfPath());
}

HdBprim* HdShiroRenderDelegate::CreateBprim(const TfToken& typeId, const SdfPath& bprimId) {
    if (typeId == HdPrimTypeTokens->renderBuffer) {
        return new HdShiroRenderBuffer(bprimId);
    }

    return nullptr;
}

HdBprim* HdShiroRenderDelegate::CreateFallbackBprim(const TfToken& typeId) {
    return CreateBprim(typeId, SdfPath());
}

void HdShiroRenderDelegate::DestroyRprim(HdRprim* rPrim) {
    delete rPrim;
}

void HdShiroRenderDelegate::DestroySprim(HdSprim* sPrim) {
    delete sPrim;
}

void HdShiroRenderDelegate::DestroyBprim(HdBprim* bPrim) {
    delete bPrim;
}

void HdShiroRenderDelegate::CommitResources(HdChangeTracker* tracker) {
    (void)tracker;
}

HdAovDescriptor HdShiroRenderDelegate::GetDefaultAovDescriptor(const TfToken& name) const {
    if (name == HdAovTokens->color) {
        return HdAovDescriptor(HdFormatFloat32Vec4, false, VtValue(GfVec4f(0.0f)));
    }

    if (name == HdAovTokens->depth || name == HdShiroTokens->depth) {
        return HdAovDescriptor(HdFormatFloat32, false, VtValue(0.0f));
    }

    if (name == HdShiroTokens->albedo || name == HdShiroTokens->normal) {
        return HdAovDescriptor(HdFormatFloat32Vec3, false, VtValue(GfVec3f(0.0f)));
    }

    return HdAovDescriptor(HdFormatInvalid, false, VtValue());
}

TfTokenVector HdShiroRenderDelegate::GetMaterialRenderContexts() const {
    return MaterialRenderContexts();
}

void HdShiroRenderDelegate::SetRenderSetting(const TfToken& key, const VtValue& value) {
    renderParam_->SetRenderSetting(key, value);
}

VtValue HdShiroRenderDelegate::GetRenderSetting(const TfToken& key) const {
    return renderParam_->GetRenderSetting(key);
}

bool HdShiroRenderDelegate::Pause() {
    renderParam_->SetPaused(true);
    return true;
}

bool HdShiroRenderDelegate::Resume() {
    renderParam_->SetPaused(false);
    return true;
}

bool HdShiroRenderDelegate::IsPauseSupported() const {
    return true;
}

void HdShiroRenderDelegate::ApplySettingsMap(const HdRenderSettingsMap& settingsMap) {
    for (const auto& [key, value] : settingsMap) {
        renderParam_->SetRenderSetting(key, value);
    }
}

PXR_NAMESPACE_CLOSE_SCOPE

#endif
