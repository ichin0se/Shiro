#pragma once

#include "shiro/core/Config.h"

#if SHIRO_WITH_USD

#include <atomic>
#include <mutex>
#include <vector>

#include <pxr/base/gf/vec3i.h>
#include <pxr/imaging/hd/types.h>
#include <pxr/imaging/hd/renderBuffer.h>

#include "shiro/render/FrameBuffer.h"

PXR_NAMESPACE_OPEN_SCOPE

class HdShiroRenderBuffer final : public HdRenderBuffer {
public:
    explicit HdShiroRenderBuffer(const SdfPath& id);
    ~HdShiroRenderBuffer() override = default;

    bool Allocate(const GfVec3i& dimensions, HdFormat format, bool multiSampled) override;
    unsigned int GetWidth() const override;
    unsigned int GetHeight() const override;
    unsigned int GetDepth() const override;
    HdFormat GetFormat() const override;
    bool IsMultiSampled() const override;
    void* Map() override;
    void Unmap() override;
    bool IsMapped() const override;
    void Resolve() override;
    bool IsConverged() const override;

    void WriteAov(const TfToken& aovName, const shiro::render::FrameBuffer& frame);
    void SetConverged(bool converged);

private:
    size_t ByteSize() const;
    void ResizeStorage();
    void _Deallocate() override;

    GfVec3i dimensions_ = GfVec3i(0, 0, 0);
    HdFormat format_ = HdFormatInvalid;
    bool multiSampled_ = false;
    std::vector<uint8_t> storage_;
    std::atomic<size_t> mapCount_ = 0;
    std::atomic<bool> converged_ = false;
    mutable std::mutex mutex_;
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif
