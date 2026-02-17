#pragma once

namespace services
{
    class ScriptInterpreter;
}

namespace core::api
{
    class WaterAPI
    {
    public:
        static void registerAPI(services::ScriptInterpreter* interpreter);
    };
}
