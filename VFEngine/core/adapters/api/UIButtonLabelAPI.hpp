#pragma once

namespace services
{
    class ScriptInterpreter;
}

namespace core::api
{
    class UIButtonLabelAPI
    {
    public:
        static void registerAPI(services::ScriptInterpreter* interpreter);
    };
}
