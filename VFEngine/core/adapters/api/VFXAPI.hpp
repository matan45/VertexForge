#pragma once

namespace services
{
    class ScriptInterpreter;
}

namespace core::api
{
    class VFXAPI
    {
    public:
        static void registerAPI(services::ScriptInterpreter* interpreter);
    };
}
