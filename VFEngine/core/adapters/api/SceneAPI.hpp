#pragma once

namespace services
{
    class ScriptInterpreter;
}

namespace core::api
{
    class SceneAPI
    {
    public:
        static void registerAPI(services::ScriptInterpreter* interpreter);
    };
}
