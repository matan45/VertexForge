#pragma once

namespace services
{
    class ScriptInterpreter;
}

namespace core::api
{
    class GameConfigAPI
    {
    public:
        static void registerAPI(services::ScriptInterpreter* interpreter);
    };
}
