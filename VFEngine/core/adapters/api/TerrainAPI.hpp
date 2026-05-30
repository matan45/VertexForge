#pragma once

namespace services
{
    class ScriptInterpreter;
}

namespace core::api
{
    class TerrainAPI
    {
    public:
        static void registerAPI(services::ScriptInterpreter* interpreter);
    };
}
