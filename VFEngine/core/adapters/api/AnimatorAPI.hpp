#pragma once

namespace services
{
    class ScriptInterpreter;
}

namespace core::api
{
    class AnimatorAPI
    {
    public:
        static void registerAPI(services::ScriptInterpreter* interpreter);
    };
}
