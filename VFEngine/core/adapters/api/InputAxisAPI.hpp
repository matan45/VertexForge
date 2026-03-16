#pragma once

namespace services
{
    class ScriptInterpreter;
}

namespace core::api
{
    class InputAxisAPI
    {
    public:
        static void registerAPI(services::ScriptInterpreter* interpreter);
    };
}
