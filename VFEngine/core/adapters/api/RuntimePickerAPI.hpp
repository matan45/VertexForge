#pragma once

namespace services
{
    class ScriptInterpreter;
}

namespace core::api
{
    class RuntimePickerAPI
    {
    public:
        static void registerAPI(services::ScriptInterpreter* interpreter);
    };
}
