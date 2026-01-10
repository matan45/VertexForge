#pragma once

namespace services
{
    class ScriptInterpreter;
}

namespace core::api
{
    class EntityAPI
    {
    public:
        static void registerAPI(services::ScriptInterpreter* interpreter);
    };
}
