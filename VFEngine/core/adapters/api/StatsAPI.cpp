// mType headers must come first to avoid Windows macro conflicts
#include <services/ScriptInterpreter.hpp>
#include <environment/NativeContext.hpp>
#include <span>

#include "StatsAPI.hpp"
#include "NativeHelpers.hpp"
#include "../../../services/events/EventDispatcher.hpp"
#include "../../../services/events/save/ConfigEvents.hpp"
#include "time/Timer.hpp"
#include "stats/GpuPassStats.hpp"
#include "stats/FrameDrawStats.hpp"
#include "types/RenderSettingsConfig.hpp"

namespace core::api
{
    namespace
    {
        constexpr const char* KEY_HUD = "gfx.hud";

        // Smoothed FPS for the on-screen readout. Advanced at most once per rendered
        // frame (guarded on the per-frame-constant elapsed time) so calling several
        // stats natives in one frame does not double-advance the EMA.
        double g_smoothedFps = 0.0;
        double g_lastElapsed = -1.0;

        void tickFps()
        {
            double now = engineTime::Timer::getElapsedTime();
            if (now != g_lastElapsed)
            {
                g_smoothedFps = types::emaFps(g_smoothedFps, engineTime::Timer::getDeltaTime(), 0.1);
                g_lastElapsed = now;
            }
        }

        double gpuMs()
        {
            auto& stats = render::GpuPassStats::instance();
            return stats.hasFrameGpuTime() ? static_cast<double>(stats.emaFrameGpuMs()) : 0.0;
        }
    }

    void StatsAPI::registerAPI(services::ScriptInterpreter* interpreter)
    {
        // Stats.getFps() — smoothed frames per second.
        interpreter->registerNativeFunction("_native_stats_getFps",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value>) -> value::Value {
                tickFps();
                return value::Value(g_smoothedFps);
            }});

        // Stats.getCpuMs() — CPU frame time in milliseconds (raw last-frame delta).
        interpreter->registerNativeFunction("_native_stats_getCpuMs",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value>) -> value::Value {
                return value::Value(engineTime::Timer::getDeltaTime() * 1000.0);
            }});

        // Stats.getGpuMs() — smoothed whole-frame GPU time (graphics queue), 0 until the
        // first timestamp readback is ready or if the device has no timestamp support.
        interpreter->registerNativeFunction("_native_stats_getGpuMs",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value>) -> value::Value {
                return value::Value(gpuMs());
            }});

        // Stats.getDrawCalls() — CPU-recorded draw commands from the last completed frame.
        interpreter->registerNativeFunction("_native_stats_getDrawCalls",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value>) -> value::Value {
                return value::Value(static_cast<int64_t>(render::FrameDrawStats::total()));
            }});

        // Stats.getHudLine() — a preformatted one-line summary, for a single UILabel.
        interpreter->registerNativeFunction("_native_stats_getHudLine",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value>) -> value::Value {
                tickFps();
                return value::Value(types::formatHudLine(
                    g_smoothedFps,
                    engineTime::Timer::getDeltaTime() * 1000.0,
                    gpuMs(),
                    render::FrameDrawStats::total()));
            }});

        // Stats.getHudEnabled() / setHudEnabled(bool) — persisted overlay toggle (off by default).
        interpreter->registerNativeFunction("_native_stats_getHudEnabled",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value>) -> value::Value {
                events::save::GetConfigBoolQuery q;
                q.key = KEY_HUD;
                q.defaultValue = false;
                return value::Value(events::EventDispatcher::instance().query(q));
            }});

        interpreter->registerNativeFunction("_native_stats_setHudEnabled",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value {
                if (args.empty()) return value::Value(std::monostate{});
                events::save::SetConfigBoolCommand cmd;
                cmd.key = KEY_HUD;
                cmd.value = extractBool(args[0], "Stats.setHudEnabled");
                events::EventDispatcher::instance().execute(cmd);
                return value::Value(std::monostate{});
            }});
    }
}
