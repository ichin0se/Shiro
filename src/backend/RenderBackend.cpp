#include "shiro/backend/RenderBackend.h"

namespace shiro::backend {

std::string_view BackendKindName(render::BackendKind kind) {
    switch (kind) {
    case render::BackendKind::Cpu:
        return "cpu";
    case render::BackendKind::Gpu:
        return "gpu";
    case render::BackendKind::Hybrid:
        return "hybrid";
    }

    return "cpu";
}

}  // namespace shiro::backend
