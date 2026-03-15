#include "shiro/hydra/RendererPlugin.h"

#if SHIRO_WITH_USD

#include <pxr/base/tf/registryManager.h>
#include <pxr/base/tf/type.h>
#include <pxr/imaging/hd/rendererPluginRegistry.h>

#include "shiro/hydra/RenderDelegate.h"

PXR_NAMESPACE_OPEN_SCOPE

TF_REGISTRY_FUNCTION(TfType) {
    HdRendererPluginRegistry::Define<HdShiroRendererPlugin>();
}

HdRenderDelegate* HdShiroRendererPlugin::CreateRenderDelegate() {
    return new HdShiroRenderDelegate();
}

HdRenderDelegate* HdShiroRendererPlugin::CreateRenderDelegate(const HdRenderSettingsMap& settingsMap) {
    return new HdShiroRenderDelegate(settingsMap);
}

void HdShiroRendererPlugin::DeleteRenderDelegate(HdRenderDelegate* renderDelegate) {
    delete renderDelegate;
}

bool HdShiroRendererPlugin::IsSupported(const HdRendererCreateArgs& rendererCreateArgs, std::string* reasonWhyNot) const {
    (void)rendererCreateArgs;
    (void)reasonWhyNot;
    return true;
}

PXR_NAMESPACE_CLOSE_SCOPE

#endif
