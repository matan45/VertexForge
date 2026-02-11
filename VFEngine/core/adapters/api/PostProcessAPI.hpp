#pragma once

namespace services
{
    class ScriptInterpreter;
}

namespace core::api
{
    class PostProcessAPI
    {
    public:
        static void registerAPI(services::ScriptInterpreter* interpreter);
    };
}
