#pragma once

namespace services
{
    class ScriptInterpreter;
}

namespace core::api
{
    class InputActionAPI
    {
    public:
        static void registerAPI(services::ScriptInterpreter* interpreter);
    };
}
