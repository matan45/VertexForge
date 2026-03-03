#pragma once

namespace services
{
    class ScriptInterpreter;
}

namespace core::api
{
    class CameraAPI
    {
    public:
        static void registerAPI(services::ScriptInterpreter* interpreter);
    };
}
