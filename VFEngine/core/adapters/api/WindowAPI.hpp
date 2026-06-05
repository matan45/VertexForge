#pragma once

namespace services
{
    class ScriptInterpreter;
}

namespace core::api
{
    class WindowAPI
    {
    public:
        static void registerAPI(services::ScriptInterpreter* interpreter);
    };
}
