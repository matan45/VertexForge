#pragma once

namespace services
{
    class ScriptInterpreter;
}

namespace core::api
{
    class BehaviorTreeAPI
    {
    public:
        static void registerAPI(services::ScriptInterpreter* interpreter);
    };
}
