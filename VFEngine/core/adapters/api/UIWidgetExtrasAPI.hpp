#pragma once

namespace services
{
    class ScriptInterpreter;
}

namespace core::api
{
    // Natives for the newer auxiliary UI widgets: tooltips and windows.
    class UIWidgetExtrasAPI
    {
    public:
        static void registerAPI(services::ScriptInterpreter* interpreter);
    };
}
