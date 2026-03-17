#pragma once

namespace services
{
    class ScriptInterpreter;
}

namespace core::api
{
    class AtmosphereAPI
    {
    public:
        static void registerAPI(services::ScriptInterpreter* interpreter);
    };
}
