#include "shiro/hydra/RendererPlugin.h"

#if SHIRO_WITH_USD

#include <pxr/base/tf/registryManager.h>
#include <pxr/base/tf/type.h>
#include <pxr/imaging/hd/rendererPluginRegistry.h>

#include "shiro/hydra/RenderDelegate.h"

PXR_NAMESPACE_OPEN_SCOPE

namespace {

HdRenderDelegate* CreateRenderDelegateForBackend(
    std::optional<shiro::render::BackendKind> backend) {
    return new HdShiroRenderDelegate(backend);
}

HdRenderDelegate* CreateRenderDelegateForBackend(
    const HdRenderSettingsMap& settingsMap,
    std::optional<shiro::render::BackendKind> backend) {
    return new HdShiroRenderDelegate(settingsMap, backend);
}

void DeleteRenderDelegateInstance(HdRenderDelegate* renderDelegate) {
    delete renderDelegate;
}

#if PXR_VERSION >= 2511
bool IsBackendSelectionSupported(
    const HdRendererCreateArgs& rendererCreateArgs,
    std::string* reasonWhyNot) {
    (void)rendererCreateArgs;
    (void)reasonWhyNot;
#else
bool IsBackendSelectionSupported(bool gpuEnabled) {
    (void)gpuEnabled;
#endif
    return true;
}

}  // namespace

TF_REGISTRY_FUNCTION(TfType) {
    HdRendererPluginRegistry::Define<HdShiroRendererPlugin>();
    HdRendererPluginRegistry::Define<HdShiroXpuRendererPlugin>();
    HdRendererPluginRegistry::Define<HdShiroGpuRendererPlugin>();
}

HdRenderDelegate* HdShiroRendererPlugin::CreateRenderDelegate() {
    return CreateRenderDelegateForBackend(shiro::render::BackendKind::Cpu);
}

HdRenderDelegate* HdShiroRendererPlugin::CreateRenderDelegate(const HdRenderSettingsMap& settingsMap) {
    return CreateRenderDelegateForBackend(settingsMap, shiro::render::BackendKind::Cpu);
}

void HdShiroRendererPlugin::DeleteRenderDelegate(HdRenderDelegate* renderDelegate) {
    DeleteRenderDelegateInstance(renderDelegate);
}

#if PXR_VERSION >= 2511
bool HdShiroRendererPlugin::IsSupported(const HdRendererCreateArgs& rendererCreateArgs, std::string* reasonWhyNot) const {
    return IsBackendSelectionSupported(rendererCreateArgs, reasonWhyNot);
#else
bool HdShiroRendererPlugin::IsSupported(bool gpuEnabled) const {
    return IsBackendSelectionSupported(gpuEnabled);
#endif
}

HdRenderDelegate* HdShiroXpuRendererPlugin::CreateRenderDelegate() {
    return CreateRenderDelegateForBackend(shiro::render::BackendKind::Hybrid);
}

HdRenderDelegate* HdShiroXpuRendererPlugin::CreateRenderDelegate(const HdRenderSettingsMap& settingsMap) {
    return CreateRenderDelegateForBackend(settingsMap, shiro::render::BackendKind::Hybrid);
}

void HdShiroXpuRendererPlugin::DeleteRenderDelegate(HdRenderDelegate* renderDelegate) {
    DeleteRenderDelegateInstance(renderDelegate);
}

#if PXR_VERSION >= 2511
bool HdShiroXpuRendererPlugin::IsSupported(const HdRendererCreateArgs& rendererCreateArgs, std::string* reasonWhyNot) const {
    return IsBackendSelectionSupported(rendererCreateArgs, reasonWhyNot);
#else
bool HdShiroXpuRendererPlugin::IsSupported(bool gpuEnabled) const {
    return IsBackendSelectionSupported(gpuEnabled);
#endif
}

HdRenderDelegate* HdShiroGpuRendererPlugin::CreateRenderDelegate() {
    return CreateRenderDelegateForBackend(shiro::render::BackendKind::Gpu);
}

HdRenderDelegate* HdShiroGpuRendererPlugin::CreateRenderDelegate(const HdRenderSettingsMap& settingsMap) {
    return CreateRenderDelegateForBackend(settingsMap, shiro::render::BackendKind::Gpu);
}

void HdShiroGpuRendererPlugin::DeleteRenderDelegate(HdRenderDelegate* renderDelegate) {
    DeleteRenderDelegateInstance(renderDelegate);
}

#if PXR_VERSION >= 2511
bool HdShiroGpuRendererPlugin::IsSupported(const HdRendererCreateArgs& rendererCreateArgs, std::string* reasonWhyNot) const {
    return IsBackendSelectionSupported(rendererCreateArgs, reasonWhyNot);
#else
bool HdShiroGpuRendererPlugin::IsSupported(bool gpuEnabled) const {
    return IsBackendSelectionSupported(gpuEnabled);
#endif
}

PXR_NAMESPACE_CLOSE_SCOPE

#endif
