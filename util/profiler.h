#ifndef PROFILER_WIN32_H
#define PROFILER_WIN32_H

#if defined(_WIN32) && defined(XEMU_SUPERLUMINAL)

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wstrict-prototypes"
#pragma clang diagnostic ignored "-Wnullability-completeness"
#elif defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wstrict-prototypes"
#endif

#include "Superluminal/PerformanceAPI_capi.h"
#include "Superluminal/PerformanceAPI_loader.h"

#ifdef __clang__
#pragma clang diagnostic pop
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

#ifndef PERFORMANCEAPI_MAKECOLOR
#define PERFORMANCEAPI_MAKECOLOR(r, g, b) \
((((unsigned int)(r) & 0xFF) << 16) | (((unsigned int)(g) & 0xFF) << 8) | ((unsigned int)(b) & 0xFF))
#endif

#ifdef __cplusplus
extern "C" {
#endif

extern PerformanceAPI_Functions __g_superluminal;

#ifdef __cplusplus
}
#endif


#define PROF_DECLARE() PerformanceAPI_Functions __g_superluminal = {0}
#define PROF_INIT(api_handle) \
    PerformanceAPI_ModuleHandle api_handle = PerformanceAPI_LoadFrom(NULL, &__g_superluminal)
#define PROF_SHUTDOWN(api_handle) PerformanceAPI_Free(&api_handle)

#define PROF_SCOPE(name) PERFORMANCEAPI_INSTRUMENT(name)
#define PROF_SCOPE_COLOR(name, color) PERFORMANCEAPI_INSTRUMENT_COLOR(name, color)

#define PROF_BEGIN(name) if (__g_superluminal.BeginEvent) \
    __g_superluminal.BeginEvent(name, NULL, PERFORMANCEAPI_DEFAULT_COLOR)
#define PROF_BEGIN_COLOR(name, r, g, b) if (__g_superluminal.BeginEvent) \
    __g_superluminal.BeginEvent(name, NULL, PERFORMANCEAPI_MAKECOLOR(r, g, b))
#define PROF_END() if (__g_superluminal.EndEvent) \
    __g_superluminal.EndEvent()

#else

#define PROF_DECLARE()
#define PROF_INIT(api_handle)
#define PROF_SHUTDOWN(api_handle)

#define PROF_SCOPE(name)
#define PROF_SCOPE_COLOR(name, color)

#define PROF_BEGIN(name)
#define PROF_BEGIN_COLOR(name, r, g, b)
#define PROF_END()

#endif

#endif /* PROFILER_WIN32_H */