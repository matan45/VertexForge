#pragma once
#include "RenderSettings.hpp"
#include <cstdint>
#include <cstdio>
#include <string>

namespace types
{
    // VK-1534 — pure, engine-free helpers for the runtime scalability surface.
    //
    // int<->enum clamping, config-driven RenderSettings assembly, and the HUD
    // number formatting all live here with NO EventDispatcher / Vulkan / graphics
    // dependency, so the script natives (core), the runtime boot-apply, and the
    // CPU test suite can each link them directly.

    // Persisted config stores enum ordinals directly (the enums are uint8_t). These
    // clamp an out-of-range int back to the neutral default rather than producing an
    // invalid enum value.
    inline RenderPreset clampPreset(int v)
    {
        if (v < 0 || v > static_cast<int>(RenderPreset::Custom))
            return RenderPreset::High;
        return static_cast<RenderPreset>(v);
    }

    inline PresentMode clampPresentMode(int v)
    {
        if (v < 0 || v > static_cast<int>(PresentMode::Immediate))
            return PresentMode::Mailbox;
        return static_cast<PresentMode>(v);
    }

    inline MsaaSamples clampMsaa(int v)
    {
        if (v < 0 || v > static_cast<int>(MsaaSamples::X8))
            return MsaaSamples::Off;
        return static_cast<MsaaSamples>(v);
    }

    // Assemble a full RenderSettings from a persisted (preset, present, msaa) triple.
    // The preset drives the scalability sub-structs via fromPreset() (shadows, culling,
    // distance culling, terrain, virtual texture, dynamic resolution, VFX/anim LOD, GI);
    // present-mode + MSAA are the player's display choices layered on top of it.
    //
    // Note: postProcess / atmosphere / cloud are left at fromPreset()'s output (defaults).
    // The runtime apply path deliberately does NOT push those — it applies only the
    // ApplyShadowSettingsCommand subset + display + VFX/anim LOD — so scene-authored
    // post-process / sky are preserved.
    inline RenderSettings buildFromConfig(int preset, int present, int msaa)
    {
        RenderSettings s = RenderSettings::fromPreset(clampPreset(preset));
        s.display.presentMode = clampPresentMode(present);
        s.display.msaa = clampMsaa(msaa);
        return s;
    }

    // Exponential moving average for the on-screen FPS readout. alpha in (0,1]; larger =
    // more responsive. prevEma <= 0 seeds with the instantaneous value; dtSeconds <= 0
    // returns prevEma unchanged (a frame with no elapsed time carries no new sample).
    inline double emaFps(double prevEma, double dtSeconds, double alpha)
    {
        if (dtSeconds <= 0.0)
            return prevEma;
        const double instant = 1.0 / dtSeconds;
        if (prevEma <= 0.0)
            return instant;
        return prevEma + alpha * (instant - prevEma);
    }

    // One-line HUD string, e.g. "FPS 60  CPU 16.7ms  GPU 12.3ms  Draws 842".
    inline std::string formatHudLine(double fps, double cpuMs, double gpuMs, uint32_t draws)
    {
        char buf[128];
        std::snprintf(buf, sizeof(buf), "FPS %.0f  CPU %.1fms  GPU %.1fms  Draws %u",
                      fps, cpuMs, gpuMs, static_cast<unsigned>(draws));
        return std::string(buf);
    }
}
