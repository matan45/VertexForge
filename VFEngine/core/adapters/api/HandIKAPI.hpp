#pragma once

namespace services
{
    class ScriptInterpreter;
}

namespace core::api
{
    class HandIKAPI
    {
    public:
        static void registerAPI(services::ScriptInterpreter* interpreter);
    };
}
