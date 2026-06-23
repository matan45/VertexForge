#pragma once

namespace services
{
    class ScriptInterpreter;
}

namespace core::api
{
    class TimeAPI
    {
    public:
        static void registerAPI(services::ScriptInterpreter* interpreter);
    };
}
