#pragma once

// VK-1535 — engine profiling shim.
//
// Wraps the Tracy client behind a single set of macros so raw Tracy includes
// never leak into engine code. When VF_TRACY is not defined (the default —
// premake only sets it when the top-of-file `enableTracy` flag is on) every
// macro compiles to nothing and this header pulls in no dependency, so default
// builds contain zero Tracy code (VK-1535 AC#2).
//
// Usage:
//   VF_ZONE();                       // RAII zone named after the enclosing function
//   VF_ZONE_NAMED("SwapchainPresent");  // RAII zone with a compile-time literal name
//   VF_ZONE_DYN(str, len);           // RAII zone with a runtime name (e.g. std::string)
//   VF_FRAME_MARK();                 // marks a frame boundary, once per frame
//   VF_THREAD_NAME("Render");        // names the calling thread in the profiler, once
//
// Each *_ZONE* macro must be placed at statement position inside the scope it
// should measure; the zone ends when that scope exits.

#ifdef VF_TRACY

#include "tracy/Tracy.hpp"

#define VF_ZONE()            ZoneScoped
#define VF_ZONE_NAMED(name)  ZoneScopedN(name)
// Two statements: open a zone, then attach a runtime name to it. Keep at
// statement position (not as the body of a brace-less if/for).
#define VF_ZONE_DYN(str, len) ZoneScoped; ZoneName((str), (len))
#define VF_FRAME_MARK()      FrameMark
#define VF_THREAD_NAME(name) tracy::SetThreadName(name)

#else

#define VF_ZONE()            ((void)0)
#define VF_ZONE_NAMED(name)  ((void)0)
#define VF_ZONE_DYN(str, len) ((void)0)
#define VF_FRAME_MARK()      ((void)0)
#define VF_THREAD_NAME(name) ((void)0)

#endif
