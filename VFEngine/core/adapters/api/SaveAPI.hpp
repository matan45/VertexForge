#pragma once

namespace services
{
    class ScriptInterpreter;
}

namespace core::api
{
    class SaveAPI
    {
    public:
        static void registerAPI(services::ScriptInterpreter* interpreter);
    };
}
