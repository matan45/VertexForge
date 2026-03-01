#pragma once

namespace services
{
    class ScriptInterpreter;
}

namespace core::api
{
    class IKAPI
    {
    public:
        static void registerAPI(services::ScriptInterpreter* interpreter);
    };
}
