#pragma once

#include "shiro/core/Config.h"

#if SHIRO_WITH_USD

#include <optional>

#include <pxr/imaging/hd/rendererPlugin.h>

#include "shiro/render/Renderer.h"

PXR_NAMESPACE_OPEN_SCOPE

class HdShiroRendererPlugin final : public HdRendererPlugin {
public:
    HdRenderDelegate* CreateRenderDelegate() override;
    HdRenderDelegate* CreateRenderDelegate(const HdRenderSettingsMap& settingsMap) override;
    void DeleteRenderDelegate(HdRenderDelegate* renderDelegate) override;
#if PXR_VERSION >= 2511
    bool IsSupported(const HdRendererCreateArgs& rendererCreateArgs, std::string* reasonWhyNot = nullptr) const override;
#else
    bool IsSupported(bool gpuEnabled = true) const override;
#endif
};

class HdShiroXpuRendererPlugin final : public HdRendererPlugin {
public:
    HdRenderDelegate* CreateRenderDelegate() override;
    HdRenderDelegate* CreateRenderDelegate(const HdRenderSettingsMap& settingsMap) override;
    void DeleteRenderDelegate(HdRenderDelegate* renderDelegate) override;
#if PXR_VERSION >= 2511
    bool IsSupported(const HdRendererCreateArgs& rendererCreateArgs, std::string* reasonWhyNot = nullptr) const override;
#else
    bool IsSupported(bool gpuEnabled = true) const override;
#endif
};

class HdShiroGpuRendererPlugin final : public HdRendererPlugin {
public:
    HdRenderDelegate* CreateRenderDelegate() override;
    HdRenderDelegate* CreateRenderDelegate(const HdRenderSettingsMap& settingsMap) override;
    void DeleteRenderDelegate(HdRenderDelegate* renderDelegate) override;
#if PXR_VERSION >= 2511
    bool IsSupported(const HdRendererCreateArgs& rendererCreateArgs, std::string* reasonWhyNot = nullptr) const override;
#else
    bool IsSupported(bool gpuEnabled = true) const override;
#endif
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif
