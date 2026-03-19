#pragma once

namespace services
{
    class ScriptInterpreter;
}

namespace core::api
{
    class PostProcessAPI
    {
    public:
        static void registerAPI(services::ScriptInterpreter* interpreter);

    private:
        static void registerCoreEffects(services::ScriptInterpreter* interpreter);
        static void registerAdvancedEffects(services::ScriptInterpreter* interpreter);
        static void registerEdgeDetectionAndColorGrading(services::ScriptInterpreter* interpreter);
    };
}
