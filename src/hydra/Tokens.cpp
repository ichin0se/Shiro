#include "shiro/hydra/Tokens.h"

#if SHIRO_WITH_USD

PXR_NAMESPACE_OPEN_SCOPE

TF_DEFINE_PUBLIC_TOKENS(HdShiroTokens,
    (shiro)
    (albedo)
    (normal)
    (depth)
    (samplesPerPixel)
    (maxDepth));

PXR_NAMESPACE_CLOSE_SCOPE

#endif
