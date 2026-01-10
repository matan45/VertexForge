#pragma once

namespace services
{
    class ScriptInterpreter;
}

namespace core::api
{
    class InputAPI
    {
    public:
        static void registerAPI(services::ScriptInterpreter* interpreter);
    };
}
