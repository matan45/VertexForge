#pragma once

namespace services
{
    class ScriptInterpreter;
}

namespace core::api
{
    class EntityComponentAPI
    {
    public:
        static void registerAPI(services::ScriptInterpreter* interpreter);
    };
}
