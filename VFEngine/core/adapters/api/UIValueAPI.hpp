#pragma once

namespace services
{
    class ScriptInterpreter;
}

namespace core::api
{
    class UIValueAPI
    {
    public:
        static void registerAPI(services::ScriptInterpreter* interpreter);
    };
}
