#pragma once

namespace services
{
    class ScriptInterpreter;
}

namespace core::api
{
    class DebugDrawAPI
    {
    public:
        static void registerAPI(services::ScriptInterpreter* interpreter);
    };
}
