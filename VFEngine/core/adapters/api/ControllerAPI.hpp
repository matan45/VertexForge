#pragma once

namespace services
{
    class ScriptInterpreter;
}

namespace core::api
{
    class ControllerAPI
    {
    public:
        static void registerAPI(services::ScriptInterpreter* interpreter);
    };
}
