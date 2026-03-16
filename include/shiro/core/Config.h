#pragma once

#ifndef SHIRO_WITH_USD
#define SHIRO_WITH_USD 0
#endif

#ifndef SHIRO_WITH_OPENQMC
#define SHIRO_WITH_OPENQMC 0
#endif

#ifndef SHIRO_HAVE_OIIO
#define SHIRO_HAVE_OIIO 0
#endif

#ifndef SHIRO_HAVE_EMBREE
#define SHIRO_HAVE_EMBREE 0
#endif

#ifndef SHIRO_HAVE_OPENSUBDIV
#define SHIRO_HAVE_OPENSUBDIV 0
#endif

#ifndef SHIRO_HAVE_CUDA
#define SHIRO_HAVE_CUDA 0
#endif

#ifndef SHIRO_HAVE_OPTIX
#define SHIRO_HAVE_OPTIX 0
#endif

#if defined(_WIN32)
#if defined(SHIRO_HD_BUILDING_LIBRARY)
#define SHIRO_HD_API __declspec(dllexport)
#else
#define SHIRO_HD_API __declspec(dllimport)
#endif
#else
#define SHIRO_HD_API
#endif
