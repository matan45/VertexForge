#pragma once

namespace services
{
    class ScriptInterpreter;
}

namespace core::api
{
    // Loading-screen progress natives (VK-1268). Exposes a single weighted
    // progress value + current phase, plus the raw per-subsystem signals, so a
    // script (LoadingScreen.mt) can drive a loading UI during scene transitions.
    class LoadingAPI
    {
    public:
        static void registerAPI(services::ScriptInterpreter* interpreter);
    };
}
