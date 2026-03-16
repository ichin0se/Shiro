#pragma once

#include "shiro/core/Config.h"

#if SHIRO_WITH_USD

#include <memory>

#include <pxr/imaging/hd/instancer.h>
#include <pxr/imaging/hd/renderDelegate.h>
#include <pxr/imaging/hd/resourceRegistry.h>

PXR_NAMESPACE_OPEN_SCOPE

class HdShiroRenderParam;

class HdShiroRenderDelegate final : public HdRenderDelegate {
public:
    HdShiroRenderDelegate();
    explicit HdShiroRenderDelegate(const HdRenderSettingsMap& settingsMap);
    ~HdShiroRenderDelegate() override;

    const TfTokenVector& GetSupportedRprimTypes() const override;
    const TfTokenVector& GetSupportedSprimTypes() const override;
    const TfTokenVector& GetSupportedBprimTypes() const override;
    HdRenderParam* GetRenderParam() const override;
    HdResourceRegistrySharedPtr GetResourceRegistry() const override;
    HdRenderPassSharedPtr CreateRenderPass(HdRenderIndex* renderIndex, const HdRprimCollection& collection) override;
    HdInstancer* CreateInstancer(HdSceneDelegate* sceneDelegate, const SdfPath& id) override;
    void DestroyInstancer(HdInstancer* instancer) override;
    HdRprim* CreateRprim(const TfToken& typeId, const SdfPath& rprimId) override;
    HdSprim* CreateSprim(const TfToken& typeId, const SdfPath& sprimId) override;
    HdSprim* CreateFallbackSprim(const TfToken& typeId) override;
    HdBprim* CreateBprim(const TfToken& typeId, const SdfPath& bprimId) override;
    HdBprim* CreateFallbackBprim(const TfToken& typeId) override;
    void DestroyRprim(HdRprim* rPrim) override;
    void DestroySprim(HdSprim* sPrim) override;
    void DestroyBprim(HdBprim* bPrim) override;
    void CommitResources(HdChangeTracker* tracker) override;
    HdAovDescriptor GetDefaultAovDescriptor(const TfToken& name) const override;
    TfTokenVector GetMaterialRenderContexts() const override;
    HdRenderSettingDescriptorList GetRenderSettingDescriptors() const override;
    unsigned int GetRenderSettingsVersion() const override;
    TfTokenVector GetRenderSettingsNamespaces() const override;
    void SetRenderSetting(const TfToken& key, const VtValue& value) override;
    VtValue GetRenderSetting(const TfToken& key) const override;
    bool Pause() override;
    bool Resume() override;
    bool IsPaused() const override;
    bool IsPauseSupported() const override;

private:
    void ApplySettingsMap(const HdRenderSettingsMap& settingsMap);

    std::unique_ptr<HdShiroRenderParam> renderParam_;
    HdResourceRegistrySharedPtr resourceRegistry_;
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif
