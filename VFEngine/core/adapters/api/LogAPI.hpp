#pragma once

namespace services
{
    class ScriptInterpreter;
}

namespace core::api
{
    class LogAPI
    {
    public:
        static void registerAPI(services::ScriptInterpreter* interpreter);
    };
}
