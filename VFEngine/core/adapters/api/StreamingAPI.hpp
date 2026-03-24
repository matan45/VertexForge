#pragma once

namespace services
{
    class ScriptInterpreter;
}

namespace core::api
{
    class StreamingAPI
    {
    public:
        static void registerAPI(services::ScriptInterpreter* interpreter);
    };
}
