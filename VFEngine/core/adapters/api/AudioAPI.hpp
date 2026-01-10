#pragma once

namespace services
{
    class ScriptInterpreter;
}

namespace core::api
{
    class AudioAPI
    {
    public:
        static void registerAPI(services::ScriptInterpreter* interpreter);
    };
}
