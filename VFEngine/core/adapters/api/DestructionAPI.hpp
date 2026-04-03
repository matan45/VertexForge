#pragma once

namespace services
{
    class ScriptInterpreter;
}

namespace core::api
{
    class DestructionAPI
    {
    public:
        static void registerAPI(services::ScriptInterpreter* interpreter);
    };
}
