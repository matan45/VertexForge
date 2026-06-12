#pragma once

namespace services
{
    class ScriptInterpreter;
}

namespace core::api
{
    class UIThemeAPI
    {
    public:
        static void registerAPI(services::ScriptInterpreter* interpreter);
    };
}
