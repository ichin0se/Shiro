#pragma once

#ifndef SHIRO_WITH_USD
#define SHIRO_WITH_USD 0
#endif

#ifndef SHIRO_WITH_OPENQMC
#define SHIRO_WITH_OPENQMC 0
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
