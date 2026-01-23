#pragma once

namespace services
{
    class ScriptInterpreter;
}

namespace core::api
{
    class LightAPI
    {
    public:
        static void registerAPI(services::ScriptInterpreter* interpreter);
    };
}
