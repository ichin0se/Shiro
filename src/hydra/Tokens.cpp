#include "shiro/hydra/Tokens.h"

#if SHIRO_WITH_USD

PXR_NAMESPACE_OPEN_SCOPE

TF_DEFINE_PUBLIC_TOKENS(HdShiroTokens,
    (shiro)
    (albedo)
    (normal)
    (depth)
    (samplesPerPixel)
    ((namespacedSamplesPerPixel, "shiro:samplesPerPixel"))
    (samplesPerUpdate)
    ((namespacedSamplesPerUpdate, "shiro:samplesPerUpdate"))
    (domeLightSamples)
    ((namespacedDomeLightSamples, "shiro:domeLightSamples"))
    (maxDepth)
    ((namespacedMaxDepth, "shiro:maxDepth"))
    (diffuseDepth)
    ((namespacedDiffuseDepth, "shiro:diffuseDepth"))
    (specularDepth)
    ((namespacedSpecularDepth, "shiro:specularDepth"))
    (backend)
    ((namespacedBackend, "shiro:backend"))
    (backgroundVisible)
    ((namespacedBackgroundVisible, "shiro:backgroundVisible"))
    (headlightEnabled)
    ((namespacedHeadlightEnabled, "shiro:headlightEnabled"))
    (threadLimit)
    ((namespacedThreadLimit, "shiro:threadLimit"))
    (maxSubdivLevel)
    ((namespacedMaxSubdivLevel, "shiro:maxSubdivLevel")));

PXR_NAMESPACE_CLOSE_SCOPE

#endif
