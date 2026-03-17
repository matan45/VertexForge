#pragma once

namespace services
{
    class ScriptInterpreter;
}

namespace core::api
{
    class CloudAPI
    {
    public:
        static void registerAPI(services::ScriptInterpreter* interpreter);
    };
}
