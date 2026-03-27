#pragma once

namespace services
{
    class ScriptInterpreter;
}

namespace core::api
{
    class OceanAPI
    {
    public:
        static void registerAPI(services::ScriptInterpreter* interpreter);
    };
}
