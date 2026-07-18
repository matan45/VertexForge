#pragma once

namespace services
{
    class ScriptInterpreter;
}

namespace core::api
{
    // VK-1534 — runtime graphics-settings natives (Graphics.mt). Lets a shipped game
    // apply a RenderPreset and set present-mode (VSync) / MSAA in-game, persisting the
    // choice to config.json so it is re-applied on the next launch. Engine-internal
    // native group (Core statically links into Runtime.exe), no plugin ABI involved.
    class GraphicsAPI
    {
    public:
        static void registerAPI(services::ScriptInterpreter* interpreter);
    };
}
