#pragma once

namespace services
{
    class ScriptInterpreter;
}

namespace core::api
{
    // VK-1534 — runtime perf-stat natives (Stats.mt). Exposes the runtime-safe stat
    // sinks (FPS/CPU ms from engineTime::Timer, GPU ms from GpuPassStats, draw calls
    // from FrameDrawStats) plus the gfx.hud toggle so a shipped game can drive an
    // off-by-default perf overlay (see PerfHud.mt). No plugin ABI involved.
    class StatsAPI
    {
    public:
        static void registerAPI(services::ScriptInterpreter* interpreter);
    };
}
