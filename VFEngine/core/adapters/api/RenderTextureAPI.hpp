#pragma once

namespace services
{
    class ScriptInterpreter;
}

namespace core::api
{
    class RenderTextureAPI
    {
    public:
        static void registerAPI(services::ScriptInterpreter* interpreter);
    };
}
