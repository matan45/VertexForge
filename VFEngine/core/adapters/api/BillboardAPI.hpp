#pragma once

namespace services
{
    class ScriptInterpreter;
}

namespace core::api
{
    class BillboardAPI
    {
    public:
        static void registerAPI(services::ScriptInterpreter* interpreter);
    };
}
