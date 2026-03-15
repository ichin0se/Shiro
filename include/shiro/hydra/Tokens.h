#pragma once

#include "shiro/core/Config.h"

#if SHIRO_WITH_USD

#include <pxr/base/tf/staticTokens.h>

PXR_NAMESPACE_OPEN_SCOPE

TF_DECLARE_PUBLIC_TOKENS(HdShiroTokens, SHIRO_HD_API,
    (shiro)
    (albedo)
    (normal)
    (depth)
    (samplesPerPixel)
    (maxDepth)
);

PXR_NAMESPACE_CLOSE_SCOPE

#endif
